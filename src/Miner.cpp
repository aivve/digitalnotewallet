// Copyright (c) 2012-2016, The CryptoNote developers, The Bytecoin developers
// Copyright (c) 2016-2019, The Karbo developers
//
// This file is part of Karbo.
//
// Karbo is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Karbo is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with Karbo.  If not, see <http://www.gnu.org/licenses/>.

#include "Miner.h"

#include <future>
#include <numeric>
#include <sstream>
#include <thread>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/limits.hpp>
#include <boost/utility/value_init.hpp>

#include <CryptoNoteConfig.h>

#include "crypto/crypto.h"
#include "crypto/cn_slow_hash.hpp"
#include "crypto/random.h"
#include "Common/CommandLine.h"
#include "Common/Math.h"
#include "Common/StringTools.h"
#include "Serialization/SerializationTools.h"

#include "CryptoNoteCore/CryptoNoteTools.h"
#include "CryptoNoteCore/CryptoNoteFormatUtils.h"
#include "CryptoNoteCore/TransactionExtra.h"

#include "Wallet/WalletRpcServerCommandsDefinitions.h"

using namespace Logging;
using namespace CryptoNote;

namespace WalletGui
{

  miner::miner(const Currency& currency, IMinerHandler& handler, Logging::ILogger& log, System::Dispatcher& dispatcher) :
    m_currency(currency),
    m_dispatcher(dispatcher),
    logger(log, "miner"),
    m_stop(true),
    m_template(boost::value_initialized<Block>()),
    m_template_no(0),
    m_diffic(0),
    m_handler(handler),
    m_pausers_count(0),
    m_threads_total(0),
    m_starter_nonce(0),
    m_last_hr_merge_time(0),
    m_hashes(0),
    m_do_mining(false),
    m_current_hash_rate(0),
    m_update_block_template_interval(15),
    m_update_merge_hr_interval(2)
  {
  }
  //-----------------------------------------------------------------------------------------------------
  miner::~miner() {
    stop();
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::set_block_template(const Block& bl, const difficulty_type& di) {
    std::lock_guard<decltype(m_template_lock)> lk(m_template_lock);

    m_template = bl;

    if (m_template.majorVersion == BLOCK_MAJOR_VERSION_2 || m_template.majorVersion == BLOCK_MAJOR_VERSION_3) {
      CryptoNote::TransactionExtraMergeMiningTag mm_tag;
      mm_tag.depth = 0;
      if (!CryptoNote::get_aux_block_header_hash(m_template, mm_tag.merkleRoot)) {
        return false;
      }

      m_template.parentBlock.baseTransaction.extra.clear();
      if (!CryptoNote::appendMergeMiningTagToExtra(m_template.parentBlock.baseTransaction.extra, mm_tag)) {
        return false;
      }
    }

    m_diffic = di;
    ++m_template_no;
    m_starter_nonce = Random::randomValue<uint32_t>();
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::on_block_chain_update() {
    if (!is_mining()) {
      return true;
    }

    return request_block_template(true, true);
  }
   //-----------------------------------------------------------------------------------------------------
  bool miner::request_block_template(bool wait_wallet_refresh, bool local_dispatcher) {
    if (wait_wallet_refresh) {
      logger(INFO) << "Give wallet some time to refresh...";
      std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    }

    Block bl = boost::value_initialized<Block>();
    difficulty_type di = 0;
    uint32_t height;
    CryptoNote::BinaryArray extra_nonce;

    uint64_t fee;
    size_t median_size;
    size_t txs_size;
    uint64_t already_generated_coins;
    uint64_t reward;
    uint64_t stake;
    Crypto::SecretKey stakeKey;

    // get block template without coinbase tx
    if (!m_handler.prepareBlockTemplate(bl, fee, m_mine_address, di, height, extra_nonce, median_size, txs_size, already_generated_coins)) {
      logger(ERROR) << "Failed to get_block_template(), stopping mining";
      return false;
    }

    // get stake amount
    if (!m_handler.getStake(bl.majorVersion, fee, height, di, median_size, already_generated_coins, txs_size, stake, reward)) {
      logger(ERROR) << "Failed to getStake(), stopping mining";
      return false;
    }

    // now get stake tx from wallet
    if (!WalletAdapter::instance().getStakeTransaction(m_mine_address_str, stake, reward, 0 /* TODO make mixin configurable*/,
                                                       height + CryptoNote::parameters::CRYPTONOTE_MINED_MONEY_UNLOCK_WINDOW_V1,
                                                       "", bl.baseTransaction, stakeKey)) {
      logger(ERROR) << "Failed to getStakeTransaction(), stopping mining";
      return false;
    }

    set_block_template(bl, di);
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::on_idle()
  {
    m_update_block_template_interval.call([&](){
      if (is_mining())
        request_block_template(false, false);
      return true;
    });

    m_update_merge_hr_interval.call([&](){
      merge_hr();
      return true;
    });

    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  uint64_t millisecondsSinceEpoch() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  }

  //-----------------------------------------------------------------------------------------------------
  void miner::merge_hr()
  {
    if(m_last_hr_merge_time && is_mining()) {
      m_current_hash_rate = m_hashes * 1000 / (millisecondsSinceEpoch() - m_last_hr_merge_time + 1);
      std::lock_guard<std::mutex> lk(m_last_hash_rates_lock);
      m_last_hash_rates.push_back(m_current_hash_rate);
      if(m_last_hash_rates.size() > 19)
        m_last_hash_rates.pop_front();

      uint64_t total_hr = std::accumulate(m_last_hash_rates.begin(), m_last_hash_rates.end(), static_cast<uint64_t>(0));
      //float hr = static_cast<float>(total_hr)/static_cast<float>(m_last_hash_rates.size())/static_cast<float>(1000);
      // logger(INFO) << "Hashrate: " << std::setprecision(2) << std::fixed << hr << " kH/s";
    }
    
    m_last_hr_merge_time = millisecondsSinceEpoch();
    m_hashes = 0;
  }

  //-----------------------------------------------------------------------------------------------------
  bool miner::is_mining()
  {
    return !m_stop;
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::start(const std::string& address, size_t threads_count)
  {   
    if (is_mining()) {
      logger(ERROR) << "Starting miner but it's already started";
      return false;
    }

    std::lock_guard<std::mutex> lk(m_threads_lock);

    if(!m_threads.empty()) {
      logger(ERROR) << "Unable to start miner because there are active mining threads";
      return false;
    }

    AccountPublicAddress adr;
    if (!m_currency.parseAccountAddressString(address, m_mine_address)) {
      logger(ERROR) << "Target account address " << address << " has wrong format, starting daemon canceled";
      return false;
    }
    m_mine_address_str = address;

    m_threads_total = static_cast<uint32_t>(threads_count);
    m_starter_nonce = Random::randomValue<uint32_t>();

    // always request block template on start
    if (!request_block_template(false, true)) {
      logger(ERROR) << "Unable to start miner because block template request was unsuccessful";
      return false;
    }

    m_stop = false;
    m_pausers_count = 0; // in case mining wasn't resumed after pause

    for (uint32_t i = 0; i != threads_count; i++) {
      m_threads.push_back(std::thread(std::bind(&miner::worker_thread, this, i)));
    }

    logger(INFO) << "Mining has started with " << threads_count << " threads, good luck!";
    return true;
  }
  
  //-----------------------------------------------------------------------------------------------------
  uint64_t miner::get_speed()
  {
    if(is_mining())
      return m_current_hash_rate;
    else
      return 0;
  }
  
  //-----------------------------------------------------------------------------------------------------
  void miner::send_stop_signal() 
  {
    m_stop = true;
  }

  //-----------------------------------------------------------------------------------------------------
  bool miner::stop()
  {
    send_stop_signal();

    std::lock_guard<std::mutex> lk(m_threads_lock);

    for (auto& th : m_threads) {
      th.detach();
    }

    m_threads.clear();
    logger(INFO) << "Mining has been stopped, " << m_threads.size() << " finished" ;
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
  void miner::on_synchronized()
  {
    if(m_do_mining) {
      start(m_mine_address_str, m_threads_total);
    }
  }
  //-----------------------------------------------------------------------------------------------------
  void miner::pause()
  {
    std::lock_guard<std::mutex> lk(m_miners_count_lock);
    ++m_pausers_count;
    if(m_pausers_count == 1 && is_mining())
      logger(TRACE) << "MINING PAUSED";
  }
  //-----------------------------------------------------------------------------------------------------
  void miner::resume()
  {
    std::lock_guard<std::mutex> lk(m_miners_count_lock);
    --m_pausers_count;
    if(m_pausers_count < 0)
    {
      m_pausers_count = 0;
      logger(ERROR) << "Unexpected miner::resume() called";
    }
    if(!m_pausers_count && is_mining())
      logger(TRACE) << "MINING RESUMED";
  }
  //-----------------------------------------------------------------------------------------------------
  bool miner::worker_thread(uint32_t th_local_index)
  {
    logger(INFO) << "Miner thread was started ["<< th_local_index << "]";
    uint32_t nonce = m_starter_nonce + th_local_index;
    difficulty_type local_diff = 0;
    uint32_t local_template_ver = 0;
    cn_pow_hash_v2 hash_ctx;
    Block b;

    while(!m_stop)
    {
      if(m_pausers_count) //anti split workaround
      {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        continue;
      }

      if(local_template_ver != m_template_no) {
        std::unique_lock<std::mutex> lk(m_template_lock);
        b = m_template;
        local_diff = m_diffic;
        lk.unlock();

        local_template_ver = m_template_no;
        nonce = m_starter_nonce + th_local_index;
      }

      if(!local_template_ver)//no any set_block_template call
      {
        logger(TRACE) << "Block template not set yet";
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        continue;
      }

      b.nonce = nonce;
      Crypto::Hash h;
      if (!m_stop && !get_block_longhash(hash_ctx, b, h)) {
        logger(ERROR) << "Failed to get block long hash";
        m_stop = true;
      }

      if (!m_stop && check_hash(h, local_diff))
      {
        //we lucky!

        logger(INFO, GREEN) << "Found block for difficulty: " 
                            << local_diff << std::endl 
                            << " pow: " << Common::podToHex(h);

        Crypto::Hash id;
        if (get_block_hash(b, id))
          logger(INFO, GREEN) << "hash: " << Common::podToHex(id);

        if(!m_handler.handle_block_found(b)) {

        } else {
          //success update, lets update config
          Common::saveStringToFile(m_config_folder_path + "/" + CryptoNote::parameters::MINER_CONFIG_FILE_NAME, storeToJson(m_config));
        }
      }

      nonce += m_threads_total;
      ++m_hashes;
    }
    logger(INFO) << "Miner thread stopped ["<< th_local_index << "]";
    return true;
  }
  //-----------------------------------------------------------------------------------------------------
}