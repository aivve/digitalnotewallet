// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2016-2021 Karbo developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QMetaEnum>

#include "CryptoNoteCore/CryptoNoteTools.h"
#include "Common/StringTools.h"
#include "CurrencyAdapter.h"
#include "NodeAdapter.h"
#include "OutputsModel.h"
#include "WalletAdapter.h"

namespace WalletGui {

enum class OutputState : quint8 {SPENT, UNSPENT};

const int OUTPUTS_MODEL_COLUMN_COUNT =
  OutputsModel::staticMetaObject.enumerator(OutputsModel::staticMetaObject.indexOfEnumerator("Columns")).keyCount();


OutputsModel::OutputsModel() : QAbstractItemModel()
{
  connect(&WalletAdapter::instance(), &WalletAdapter::reloadWalletTransactionsSignal, this, &OutputsModel::reloadWalletTransactions,
          Qt::QueuedConnection);

  connect(&WalletAdapter::instance(), &WalletAdapter::walletTransactionCreatedSignal, this,
          static_cast<void(OutputsModel::*)(CryptoNote::TransactionId)>(&OutputsModel::appendTransaction), Qt::QueuedConnection);

  connect(&WalletAdapter::instance(), &WalletAdapter::walletTransactionUpdatedSignal, this,
          &OutputsModel::appendTransaction, Qt::QueuedConnection);

  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &OutputsModel::reset,
          Qt::QueuedConnection);
}

OutputsModel::~OutputsModel() {
}

OutputsModel& OutputsModel::instance() {
  static OutputsModel inst;
  return inst;
}

Qt::ItemFlags OutputsModel::flags(const QModelIndex& _index) const {
  Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemNeverHasChildren | Qt::ItemIsSelectable;

  return flags;
}

int OutputsModel::columnCount(const QModelIndex& _parent) const {
  return OUTPUTS_MODEL_COLUMN_COUNT;
}

int OutputsModel::rowCount(const QModelIndex& _parent) const {
  return m_outputs.size();
}

QVariant OutputsModel::headerData(int _section, Qt::Orientation _orientation, int _role) const {
  if(_orientation != Qt::Horizontal) {
    return QVariant();
  }

  switch(_role) {
  case Qt::DisplayRole:
    switch(_section) {
    case COLUMN_STATE:
      return tr("Status");
    case COLUMN_TYPE:
      return tr("Type");
    case COLUMN_OUTPUT_KEY:
      return tr("Key");
    case COLUMN_TX_HASH:
      return tr("Transaction hash");
    case COLUMN_AMOUNT:
      return tr("Amount");
    case COLUMN_GLOBAL_OUTPUT_INDEX:
      return tr("Global index");
    case COLUMN_OUTPUT_IN_TRANSACTION:
      return tr("Index in transaction");
    case COLUMN_SPENDING_BLOCK_HEIGHT:
      return tr("Spent at height");
    case COLUMN_TIMESTAMP:
      return tr("Timestamp");
    case COLUMN_SPENDING_TRANSACTION_HASH:
      return tr("Spent in transaction");
    case COLUMN_KEY_IMAGE:
      return tr("Key image");
    case COLUMN_INPUT_IN_TRANSACTION:
      return tr("As input");
    default:
      break;
    }

  case ROLE_COLUMN:
    return _section;
  }

  return QVariant();
}

QVariant OutputsModel::data(const QModelIndex& _index, int _role) const {
  if(!_index.isValid()) {
    return QVariant();
  }

  CryptoNote::TransactionSpentOutputInformation _output = m_outputs.value(_index.row());

  switch(_role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    return getDisplayRole(_index);

  case Qt::DecorationRole:
    return getDecorationRole(_index);

  case Qt::TextAlignmentRole:
    return getAlignmentRole(_index);

  default:
    return getUserRole(_index, _role, _output);
  }

  return QVariant();
}

QModelIndex OutputsModel::index(int _row, int _column, const QModelIndex& _parent) const {
  if(_parent.isValid()) {
    return QModelIndex();
  }

  return createIndex(_row, _column, _row);
}

QModelIndex OutputsModel::parent(const QModelIndex& _index) const {
  return QModelIndex();
}

QVariant OutputsModel::getAlignmentRole(const QModelIndex& _index) const {
  return headerData(_index.column(), Qt::Horizontal, Qt::TextAlignmentRole);
}

QVariant OutputsModel::getDecorationRole(const QModelIndex& _index) const {
  if(_index.column() == COLUMN_STATE) {
    OutputState state = static_cast<OutputState>(_index.data(ROLE_STATE).value<quint8>());
    if (state == OutputState::SPENT) {
      return QPixmap(":icons/tx-output").scaled(20, 20, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    } else if (state == OutputState::UNSPENT) {
      return QPixmap(":icons/tx-input").scaled(20, 20, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
  }

  return QVariant();
}

QVariant OutputsModel::getDisplayRole(const QModelIndex& _index) const {
  switch(_index.column()) {

  case COLUMN_STATE: {
    OutputState state = static_cast<OutputState>(_index.data(ROLE_STATE).value<quint8>());
    if (state == OutputState::SPENT) {
      return tr("Spent");
    } else if(state == OutputState::UNSPENT) {
      return tr("Unspent");
    }

    return QVariant();
  }

  case COLUMN_TYPE: {
    OutputType type = static_cast<OutputType>(_index.data(ROLE_TYPE).value<quint8>());
    if (type == OutputType::Key)
      return tr("Key");
    else if (type == OutputType::Multisignature)
      return tr("Multisignature");
    else
      return tr("Invalid");
  }

  case COLUMN_OUTPUT_KEY:
    return _index.data(ROLE_OUTPUT_KEY).toByteArray().toHex().toUpper();

  case COLUMN_TX_HASH:
    return _index.data(ROLE_TX_HASH).toByteArray().toHex().toUpper();

  case COLUMN_AMOUNT: {
    qint64 amount = _index.data(ROLE_AMOUNT).value<qint64>();
    return CurrencyAdapter::instance().formatAmount(amount);
  }

  case COLUMN_GLOBAL_OUTPUT_INDEX:
    return _index.data(ROLE_GLOBAL_OUTPUT_INDEX).value<qint32>();

  case COLUMN_OUTPUT_IN_TRANSACTION:
    return _index.data(ROLE_OUTPUT_IN_TRANSACTION).value<qint32>();

  case COLUMN_SPENDING_BLOCK_HEIGHT:
    return _index.data(ROLE_SPENDING_BLOCK_HEIGHT).value<qint32>();

  case COLUMN_TIMESTAMP: {
    QDateTime date = _index.data(ROLE_TIMESTAMP).toDateTime();
    return (date.isNull() || !date.isValid() ? "-" : date.toString("dd-MM-yy HH:mm"));
  }

  case COLUMN_SPENDING_TRANSACTION_HASH:
    return _index.data(ROLE_SPENDING_TRANSACTION_HASH).toByteArray().toHex().toUpper();

  case COLUMN_KEY_IMAGE:
    return _index.data(ROLE_KEY_IMAGE).toByteArray().toHex().toUpper();

  case COLUMN_INPUT_IN_TRANSACTION:
    return _index.data(ROLE_INPUT_IN_TRANSACTION).value<qint32>();

  default:
    break;
  }

  return QVariant();
}

QVariant OutputsModel::getUserRole(const QModelIndex& _index, int _role, CryptoNote::TransactionSpentOutputInformation _output) const {
  switch(_role) {

  case ROLE_STATE: {
    if (_output.spendingTransactionHash != CryptoNote::NULL_HASH)
      return static_cast<quint8>(OutputState::SPENT);
    else
      return static_cast<quint8>(OutputState::UNSPENT);
  }

  case ROLE_TYPE:
    return static_cast<quint8>(_output.type);

  case ROLE_TX_HASH:
    return QByteArray(reinterpret_cast<char*>(&_output.transactionHash), sizeof(_output.transactionHash));

  case ROLE_OUTPUT_KEY:
    return QByteArray(reinterpret_cast<char*>(&_output.outputKey), sizeof(_output.outputKey));

  case ROLE_AMOUNT:
    return static_cast<quint64>(_output.amount);

  case ROLE_GLOBAL_OUTPUT_INDEX:
    return static_cast<quint32>(_output.globalOutputIndex);

  case ROLE_OUTPUT_IN_TRANSACTION:
    return static_cast<quint32>(_output.outputInTransaction);

  case ROLE_SPENDING_BLOCK_HEIGHT:
    return _output.spendingBlockHeight;

  case ROLE_TIMESTAMP:
    return (_output.timestamp > 0 ? QDateTime::fromTime_t(_output.timestamp) : QDateTime());

  case ROLE_SPENDING_TRANSACTION_HASH:
    return QByteArray(reinterpret_cast<char*>(&_output.spendingTransactionHash), sizeof(_output.spendingTransactionHash));

  case ROLE_KEY_IMAGE:
    return QByteArray(reinterpret_cast<char*>(&_output.keyImage), sizeof(_output.keyImage));

  case ROLE_INPUT_IN_TRANSACTION:
    return static_cast<quint32>(_output.inputInTransaction);

  case ROLE_ROW:
    return _index.row();
  }

  return QVariant();
}

void OutputsModel::reloadWalletTransactions() {
  reset();

  m_unspentOutputs = QVector<CryptoNote::TransactionOutputInformation>::fromStdVector(WalletAdapter::instance().getOutputs());
  m_spentOutputs = QVector<CryptoNote::TransactionSpentOutputInformation>::fromStdVector(WalletAdapter::instance().getSpentOutputs());

  quint32 unspentCount = m_unspentOutputs.size();
  quint32 spentCount = m_spentOutputs.size();

  std::cout << "Unspent outputs count " << unspentCount << std::endl;
  std::cout << "Spent outputs count " << spentCount << std::endl;

  quint32 outputsCount = unspentCount + spentCount;

  std::cout << "Total outputs count " << outputsCount << std::endl;

  // just append unspent to spent and show them together
  m_outputs.append(m_spentOutputs);

  for (const auto& o : m_unspentOutputs) {
    CryptoNote::TransactionSpentOutputInformation s = *static_cast<const CryptoNote::TransactionSpentOutputInformation *>(&o);

    s.spendingBlockHeight = std::numeric_limits<uint32_t>::max();
    s.spendingTransactionHash = CryptoNote::NULL_HASH;
    s.timestamp = 0;
    s.keyImage = {};
    s.inputInTransaction = std::numeric_limits<uint32_t>::max();

    m_outputs.append(s);
  }

  beginInsertRows(QModelIndex(), 0, outputsCount - 1);
  endInsertRows();
}

void OutputsModel::appendTransaction(CryptoNote::TransactionId _id) {
  reloadWalletTransactions();
}

void OutputsModel::reset() {
  beginResetModel();
  m_outputs.clear();
  m_spentOutputs.clear();
  endResetModel();
}

}
