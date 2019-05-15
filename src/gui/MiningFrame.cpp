// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2015-2016 XDN developers
// Copyright (c) 2016-2017 The Karbowanec developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QThread>
#include <QUrl>

#include "MiningFrame.h"
#include "MainWindow.h"
#include "Settings.h"
#include "WalletAdapter.h"
#include "NodeAdapter.h"
#include "CryptoNoteWrapper.h"
#include "CurrencyAdapter.h"

#include "ui_miningframe.h"

namespace WalletGui {

const quint32 HASHRATE_TIMER_INTERVAL = 1000;

MiningFrame::MiningFrame(QWidget* _parent) : QFrame(_parent), m_ui(new Ui::MiningFrame), m_soloHashRateTimerId(-1) {
  m_ui->setupUi(this);
  initCpuCoreList();

  QString connection = Settings::instance().getConnection();
  if (connection.compare("remote") == 0) {
    m_ui->m_startSolo->setDisabled(true);
  }

  m_ui->m_startSolo->setEnabled(false);

  m_ui->m_hashRateChart->addGraph();
  m_ui->m_hashRateChart->graph(0)->setScatterStyle(QCPScatterStyle::ssDot);
  m_ui->m_hashRateChart->graph(0)->setLineStyle(QCPGraph::lsLine);
  m_ui->m_hashRateChart->graph()->setPen(QPen(QRgb(0x34496d)));
  m_ui->m_hashRateChart->graph()->setBrush(QBrush(QRgb(0xcbdef7)));

  QSharedPointer<QCPAxisTickerDateTime> dateTicker(new QCPAxisTickerDateTime);
  dateTicker->setDateTimeFormat("hh:mm:ss");
  m_ui->m_hashRateChart->xAxis->setTicker(dateTicker);

  m_ui->m_hashRateChart->yAxis->setRange(0, m_maxHr);
  m_ui->m_hashRateChart->yAxis->setLabel("Hashrate");

  // make top and right axes visible but without ticks and labels
  m_ui->m_hashRateChart->xAxis2->setVisible(true);
  m_ui->m_hashRateChart->yAxis2->setVisible(true);
  m_ui->m_hashRateChart->xAxis2->setTicks(false);
  m_ui->m_hashRateChart->yAxis2->setTicks(false);
  m_ui->m_hashRateChart->xAxis2->setTickLabels(false);
  m_ui->m_hashRateChart->yAxis2->setTickLabels(false);

  m_ui->m_hashRateChart->setBackground(QBrush(QRgb(0xeef5fc)));

  addPoint(QDateTime::currentDateTime().toTime_t(), 0);
  plot();

  connect(&WalletAdapter::instance(), &WalletAdapter::walletCloseCompletedSignal, this, &MiningFrame::walletClosed, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletInitCompletedSignal, this, &MiningFrame::walletOpened, Qt::QueuedConnection);
  connect(&WalletAdapter::instance(), &WalletAdapter::walletSynchronizationCompletedSignal, this, &MiningFrame::enableSolo, Qt::QueuedConnection);
}

MiningFrame::~MiningFrame() {
  stopSolo();
}

void MiningFrame::addPoint(double x, double y)
{
  m_hX.append(x);
  m_hY.append(y);
}

void MiningFrame::plot()
{
  m_maxHr = std::max<double>(m_maxHr, m_hY.at(m_hY.size()-1));
  m_ui->m_hashRateChart->graph(0)->setData(m_hX, m_hY);
  m_ui->m_hashRateChart->xAxis->setRange(m_hX.at(0), m_hX.at(m_hX.size()-1));
  m_ui->m_hashRateChart->yAxis->setRange(m_hY.at(0), m_maxHr);
  m_ui->m_hashRateChart->replot();
  m_ui->m_hashRateChart->update();
}

void MiningFrame::enableSolo() {
  m_sychronized = true;
  if (!m_solo_mining) {
    m_ui->m_startSolo->setEnabled(true);
    if(Settings::instance().isMiningOnLaunchEnabled() && m_sychronized) {
      if (!m_solo_mining) {
        startSolo();
        m_ui->m_startSolo->setChecked(true);
      }
    }
  }
}

void MiningFrame::timerEvent(QTimerEvent* _event) {
  if (_event->timerId() == m_soloHashRateTimerId) {
    quint64 soloHashRate = NodeAdapter::instance().getSpeed();
    if (soloHashRate == 0) {
      return;
    }
    double kHashRate = soloHashRate / 1000.0;
    m_ui->m_soloLabel->setText(tr("Mining solo. Hashrate: %1 kH/s").arg(kHashRate));
    addPoint(QDateTime::currentDateTime().toTime_t(), kHashRate);
    plot();

    return;
  }

  QFrame::timerEvent(_event);
}

void MiningFrame::initCpuCoreList() {
  quint16 threads = Settings::instance().getMiningThreads();
  int cpuCoreCount = QThread::idealThreadCount();
  if (cpuCoreCount == -1) {
      cpuCoreCount = 2;
  }

  for (int i = 0; i < cpuCoreCount; ++i) {
    m_ui->m_cpuCombo->addItem(QString::number(i + 1), i + 1);
  }

  if (threads > 0) {
    m_ui->m_cpuCombo->setCurrentIndex(m_ui->m_cpuCombo->findData(threads, Qt::DisplayRole));
  } else {
    m_ui->m_cpuCombo->setCurrentIndex((cpuCoreCount - 1) / 2);
  }
}

void MiningFrame::walletOpened() {
  if(m_solo_mining)
    stopSolo();

  m_wallet_closed = false;
  if(m_sychronized) {
    m_ui->m_stopSolo->isChecked();
    m_ui->m_stopSolo->setEnabled(false);
    m_ui->m_startSolo->setEnabled(true);
  }

  m_walletAddress = WalletAdapter::instance().getAddress();

  if(Settings::instance().isMiningOnLaunchEnabled() && m_sychronized) {
    if (!m_solo_mining) {
      startSolo();
      m_ui->m_startSolo->setChecked(true);
    }
  }
}

void MiningFrame::walletClosed() {
  // allow mining after wallet is closed to it's address
  // but mining can't be started if there's no open wallet
  // stopSolo();
  m_wallet_closed = true;
  m_ui->m_startSolo->setEnabled(false);
  m_ui->m_stopSolo->isChecked();
}

void MiningFrame::startSolo() {
  NodeAdapter::instance().startSoloMining(m_walletAddress, m_ui->m_cpuCombo->currentData().toUInt());
  m_ui->m_soloLabel->setText(tr("Starting solo mining..."));
  m_soloHashRateTimerId = startTimer(HASHRATE_TIMER_INTERVAL);
  addPoint(QDateTime::currentDateTime().toTime_t(), 0);
  m_ui->m_startSolo->setEnabled(false);
  m_ui->m_stopSolo->setEnabled(true);
  m_solo_mining = true;
}

void MiningFrame::stopSolo() {
  if(m_solo_mining) {
  killTimer(m_soloHashRateTimerId);
  m_soloHashRateTimerId = -1;
  NodeAdapter::instance().stopSoloMining();
  addPoint(QDateTime::currentDateTime().toTime_t(), 0);
  m_ui->m_soloLabel->setText(tr("Stopped"));
  }
}

void MiningFrame::startStopSoloClicked(QAbstractButton* _button) {
  if (_button == m_ui->m_startSolo && m_ui->m_startSolo->isChecked() && m_wallet_closed != true) {
    startSolo();
  } else if (m_wallet_closed == true && _button == m_ui->m_stopSolo && m_ui->m_stopSolo->isChecked()) {
    m_ui->m_startSolo->setEnabled(false);
    stopSolo();
  } else if (_button == m_ui->m_stopSolo && m_ui->m_stopSolo->isChecked()) {
    stopSolo();
  }
}

void MiningFrame::setMiningThreads() {
  Settings::instance().setMiningThreads(m_ui->m_cpuCombo->currentText().toInt());
}

}
