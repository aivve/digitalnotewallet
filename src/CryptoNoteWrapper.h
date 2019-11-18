// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2016-2017 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <INode.h>

namespace CryptoNote {

class INode;
class IWalletLegacy;
class Currency;
class CoreConfig;
class NetNodeConfig;

}

namespace Logging {
  class LoggerManager;
}

namespace WalletGui {

class Node {
public:
  virtual ~Node() = 0;
  virtual void init(const std::function<void(std::error_code)>& callback) = 0;
  virtual void deinit() = 0;
  
  virtual std::string convertPaymentId(const std::string& paymentIdString) = 0;
  virtual std::string extractPaymentId(const std::string& extra) = 0;
  virtual uint64_t getLastKnownBlockHeight() const = 0;
  virtual uint64_t getLastLocalBlockHeight() const = 0;
  virtual uint64_t getLastLocalBlockTimestamp() const = 0;
  virtual uint64_t getPeerCount() = 0;
  virtual uint64_t getDifficulty() = 0;
  virtual uint64_t getTxCount() = 0;
  virtual uint64_t getTxPoolSize() = 0;
  virtual uint64_t getAltBlocksCount() = 0;
  virtual uint64_t getConnectionsCount() = 0;
  virtual uint64_t getOutgoingConnectionsCount() = 0;
  virtual uint64_t getIncomingConnectionsCount() = 0;
  virtual uint64_t getWhitePeerlistSize() = 0;
  virtual uint64_t getGreyPeerlistSize() = 0;
  virtual uint64_t getMinimalFee() = 0;
  virtual uint8_t getCurrentBlockMajorVersion() = 0;
  virtual CryptoNote::BlockHeaderInfo getLastLocalBlockHeaderInfo() = 0;

  virtual void startMining(const std::string& address, size_t threads_count) = 0;
  virtual void stopMining() = 0;
  virtual uint64_t getSpeed() = 0;
  
  virtual bool getStake(uint8_t blockMajorVersion, uint64_t fee, uint32_t& height, CryptoNote::difficulty_type& next_diff, size_t& medianSize, uint64_t& alreadyGeneratedCoins, size_t currentBlockSize, uint64_t& stake, uint64_t& blockReward) = 0;
  virtual bool prepareBlockTemplate(CryptoNote::Block& b, uint64_t& fee, const CryptoNote::AccountPublicAddress& adr, CryptoNote::difficulty_type& diffic, uint32_t& height, const CryptoNote::BinaryArray& ex_nonce, size_t& median_size, size_t& txs_size, uint64_t& already_generated_coins) = 0;
  virtual bool handleBlockFound(CryptoNote::Block& b) = 0;

  virtual CryptoNote::IWalletLegacy* createWallet() = 0;
};

class INodeCallback {
public:
  virtual void peerCountUpdated(Node& node, size_t count) = 0;
  virtual void localBlockchainUpdated(Node& node, uint64_t height) = 0;
  virtual void lastKnownBlockHeightUpdated(Node& node, uint64_t height) = 0;
};

Node* createRpcNode(const CryptoNote::Currency& currency, INodeCallback& callback, Logging::LoggerManager& logManager, const std::string& nodeHost, unsigned short nodePort);
Node* createInprocessNode(const CryptoNote::Currency& currency, Logging::LoggerManager& logManager,
  const CryptoNote::CoreConfig& coreConfig, const CryptoNote::NetNodeConfig& netNodeConfig, INodeCallback& callback);

}
