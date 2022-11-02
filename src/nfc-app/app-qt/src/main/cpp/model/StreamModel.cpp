/*

  Copyright (c) 2021 Jose Vicente Campos Martinez - <josevcm@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#include <QDebug>

#include <QFont>
#include <QLabel>
#include <QQueue>
#include <QDateTime>
#include <QReadLocker>

#include <nfc/Nfc.h>
#include <nfc/NfcFrame.h>

#include "StreamModel.h"

static QMap<int, QString> NfcACmd = {
      {0x1A, "AUTH"}, // MIFARE Ultralight C authentication
      {0x1B, "PWD_AUTH"}, // MIFARE Ultralight EV1
      {0x26, "REQA"}, // ISO/IEC 14443
      {0x30, "READ"}, // MIFARE Ultralight EV1
      {0x39, "READ_CNT"}, // MIFARE Ultralight EV1
      {0x3A, "FAST_READ"}, // MIFARE Ultralight EV1
      {0x3C, "READ_SIG"}, // MIFARE Ultralight EV1
      {0x3E, "TEARING"}, // MIFARE Ultralight EV1
      {0x4B, "VCSL"}, // MIFARE Ultralight EV1
      {0x50, "HLTA"}, // ISO/IEC 14443
      {0x52, "WUPA"}, // ISO/IEC 14443
      {0x60, "AUTH"}, // MIFARE Classic
      {0x61, "AUTH"},// MIFARE Classic EV1
      {0x93, "SEL1"}, // ISO/IEC 14443
      {0x95, "SEL2"}, // ISO/IEC 14443
      {0x97, "SEL3"}, // ISO/IEC 14443
      {0xA0, "COMP_WRITE"}, // MIFARE Ultralight EV1
      {0xA2, "WRITE"}, // MIFARE Ultralight EV1
      {0xA5, "INCR_CNT"}, // MIFARE Ultralight EV1
      {0xE0, "RATS"}
};

static QMap<int, QString> NfcAResp = {
      {0x26, "ATQA"},
      {0x52, "ATQA"}
};

static QMap<int, QString> NfcBCmd = {
      {0x05, "REQB"},
      {0x1d, "ATTRIB"},
      {0x50, "HLTB"}
};

static QMap<int, QString> NfcBResp = {
      {0x05, "ATQB"},
};

static QMap<int, QString> NfcFCmd = {
      {0x00, "REQC"},
};

static QMap<int, QString> NfcFResp = {
      {0x00, "ATQC"},
};

static QMap<int, QString> NfcVCmd = {
      {0x01, "Inventory"},
      {0x02, "StayQuiet"},
      {0x20, "ReadBlock"},
      {0x21, "WriteBlock"},
      {0x22, "LockBlock"},
      {0x23, "ReadBlocks"},
      {0x24, "WriteBlocks"},
      {0x25, "Select"},
      {0x26, "Reset"},
      {0x27, "WriteAFI"},
      {0x28, "LockAFI"},
      {0x29, "WriteDSFID"},
      {0x2a, "LockDSFID"},
      {0x2b, "SysInfo"},
      {0x2c, "GetSecurity"}
};

struct StreamModel::Impl
{
   // time format
   int timeFormat = StreamModel::ElapsedTimeFormat;

   // fonts
   QFont defaultFont;
   QFont requestDefaultFont;
   QFont responseDefaultFont;

   // table header
   QVector<QString> headers;

   // frame list
   QList<nfc::NfcFrame *> frames;

   // frame stream
   QQueue<nfc::NfcFrame> stream;

   // stream lock
   QReadWriteLock lock;

   explicit Impl()
   {
      headers << "#" << "Time" << "Delta" << "Rate" << "Type" << "Event" << "" << "Frame";

      // request fonts
      requestDefaultFont.setBold(true);

      // response fonts
      responseDefaultFont.setItalic(true);
   }

   ~Impl()
   {
      qDeleteAll(frames);
   }

   inline QString frameTime(const nfc::NfcFrame *frame)
   {
      switch (timeFormat)
      {
         case DateTimeFormat:
         {
            double epochDateTime = frame->dateTime(); // frame date time from epoch, with microseconds in fractional part
            long epochSeconds = long(epochDateTime); // frame date time from epoch, only seconds
            double epochFraction = epochDateTime - long(epochDateTime); // frame microseconds offset

            QDateTime dateTime = QDateTime::fromSecsSinceEpoch(epochSeconds);

            return dateTime.toString("yy-MM-dd hh:mm:ss") + QString(".%1").arg(long(epochFraction * 1E3), 3, 10, QChar('0'));
         }

         default:
         {
            return QString("%1").arg(frame->timeStart(), 9, 'f', 6);
         }
      }
   }

   inline static QString frameDelta(const nfc::NfcFrame *frame, const nfc::NfcFrame *prev)
   {
      if (!prev)
         return "";

      double elapsed = frame->timeStart() - prev->timeEnd();

      if (elapsed < 20E-3)
         return QString("%1 us").arg(elapsed * 1000000, 3, 'f', 0);

      if (elapsed < 1)
         return QString("%1 ms").arg(elapsed * 1000, 3, 'f', 0);

      return QString("%1 s").arg(elapsed, 3, 'f', 0);
   }

   inline static QString frameRate(const nfc::NfcFrame *frame)
   {
      if (frame->isPollFrame() || frame->isListenFrame())
         return QString("%1k").arg(double(frame->frameRate() / 1000.0f), 3, 'f', 0);

      return {};
   }

   inline static QString frameTech(const nfc::NfcFrame *frame)
   {
      if (frame->isNfcA())
         return "NfcA";

      if (frame->isNfcB())
         return "NfcB";

      if (frame->isNfcF())
         return "NfcF";

      if (frame->isNfcV())
         return "NfcV";

      return {};
   }

   QString frameEvent(const nfc::NfcFrame *frame, const nfc::NfcFrame *prev)
   {
      if (frame->isCarrierOn())
         return {"RF-On"};

      if (frame->isCarrierOff())
         return {"RF-Off"};

      switch (frame->techType())
      {
         case nfc::TechType::NfcA:

            return eventNfcA(frame, prev);

         case nfc::TechType::NfcB:

            return eventNfcB(frame, prev);

         case nfc::TechType::NfcF:

            return eventNfcF(frame, prev);

         case nfc::TechType::NfcV:

            return eventNfcV(frame, prev);
      }

      return {};
   }

   inline static int frameFlags(const nfc::NfcFrame *frame)
   {
      return frame->frameFlags() << 8 | frame->frameType();
   }

   inline static QString frameData(const nfc::NfcFrame *frame)
   {
      QByteArray data;

      for (int i = 0; i < frame->limit(); i++)
      {
         data.append((*frame)[i]);
      }

      return {data.toHex(' ')};
   }

//   inline static QString frameData(const nfc::NfcFrame *frame)
//   {
//      QString text;
//
//      for (int i = 0; i < frame->available(); i++)
//      {
//         text.append(QString("%1 ").arg((*frame)[i], 2, 16, QLatin1Char('0')));
//      }
//
//      if (!frame->isEncrypted())
//      {
//         if (frame->hasCrcError())
//            text.append("[ECRC]");
//
//         if (frame->hasParityError())
//            text.append("[EPAR]");
//
//         if (frame->hasSyncError())
//            text.append("[ESYNC]");
//      }
//
//      return text.trimmed();
//   }

   inline static QString eventNfcA(const nfc::NfcFrame *frame, const nfc::NfcFrame *prev)
   {
      QString result;

      // skip encrypted frames
      if (frame->isEncrypted())
         return {};

      if (frame->isPollFrame())
      {
         int command = (*frame)[0];

         // Protocol Parameter Selection
         if (command == 0x50 && frame->limit() == 4)
            return "HALT";

         // Protocol Parameter Selection
         if ((command & 0xF0) == 0xD0 && frame->limit() == 5)
            return "PPS";

         if (!(result = eventIsoDep(frame)).isEmpty())
            return result;

         if (NfcACmd.contains(command))
            return NfcACmd[command];
      }
      else if (prev && prev->isPollFrame())
      {
         int command = (*prev)[0];

         if (command == 0x93 || command == 0x95 || command == 0x97)
         {
            if (frame->limit() == 3)
               return "SAK";

            if (frame->limit() == 5)
               return "UID";
         }

         if (command == 0xE0 && (*frame)[0] == (frame->limit() - 2))
            return "ATS";

         if (!(result = eventIsoDep(frame)).isEmpty())
            return result;

         if (NfcAResp.contains(command))
            return NfcAResp[command];
      }

      return {};
   }

   inline static QString eventNfcB(const nfc::NfcFrame *frame, const nfc::NfcFrame *prev)
   {
      QString result;

      if (frame->isPollFrame())
      {
         int command = (*frame)[0];

         if (!(result = eventIsoDep(frame)).isEmpty())
            return result;

         if (NfcBCmd.contains(command))
            return NfcBCmd[command];
      }
      else if (frame->isListenFrame())
      {
         int command = (*frame)[0];

         if (!(result = eventIsoDep(frame)).isEmpty())
            return result;

         if (NfcBResp.contains(command))
            return NfcBResp[command];
      }

      return {};
   }

   inline static QString eventNfcF(const nfc::NfcFrame *frame, const nfc::NfcFrame *prev)
   {
      int command = (*frame)[1];

      if (frame->isPollFrame())
      {
         if (NfcFCmd.contains(command))
            return NfcFCmd[command];

         return QString("CMD %1").arg(command, 2, 16, QChar('0'));
      }
      else if (frame->isListenFrame())
      {
         if (NfcFResp.contains(command))
            return NfcFResp[command];
      }

      return {};
   }

   inline static QString eventNfcV(const nfc::NfcFrame *frame, const nfc::NfcFrame *prev)
   {
      if (frame->isPollFrame())
      {
         int command = (*frame)[1];

         if (NfcVCmd.contains(command))
            return NfcVCmd[command];

         return QString("CMD %1").arg(command, 2, 16, QChar('0'));
      }

      return {};
   }

   inline static QString eventIsoDep(const nfc::NfcFrame *frame)
   {
      int command = (*frame)[0];

      // ISO-DEP protocol S(Deselect)
      if ((command & 0xF7) == 0xC2 && frame->limit() >= 3 && frame->limit() <= 4)
         return "S(Deselect)";

      // ISO-DEP protocol S(WTX)
      if ((command & 0xF7) == 0xF2 && frame->limit() >= 3 && frame->limit() <= 4)
         return "S(WTX)";

      // ISO-DEP protocol R(ACK)
      if ((command & 0xF6) == 0xA2 && frame->limit() == 3)
         return "R(ACK)";

      // ISO-DEP protocol R(NACK)
      if ((command & 0xF6) == 0xB2 && frame->limit() == 3)
         return "R(NACK)";

      // ISO-DEP protocol I-Block
      if ((command & 0xE2) == 0x02 && frame->limit() >= 4)
         return "I-Block";

      // ISO-DEP protocol R-Block
      if ((command & 0xE6) == 0xA2 && frame->limit() == 3)
         return "R-Block";

      // ISO-DEP protocol S-Block
      if ((command & 0xC7) == 0xC2 && frame->limit() >= 3 && frame->limit() <= 4)
         return "S-Block";

      return {};
   }
};

StreamModel::StreamModel(QObject *parent) : QAbstractTableModel(parent), impl(new Impl)
{
}

int StreamModel::rowCount(const QModelIndex &parent) const
{
   return impl->frames.size();
}

int StreamModel::columnCount(const QModelIndex &parent) const
{
   return impl->headers.size();
}

QVariant StreamModel::data(const QModelIndex &index, int role) const
{
   if (!index.isValid() || index.row() >= impl->frames.size() || index.row() < 0)
      return {};

   nfc::NfcFrame *prev = nullptr;

   auto frame = impl->frames.at(index.row());

   if (index.row() > 0)
      prev = impl->frames.at(index.row() - 1);

   if (role == Qt::DisplayRole || role == Qt::UserRole)
   {
      switch (index.column())
      {
         case Columns::Id:
            return index.row();

         case Columns::Time:
            return impl->frameTime(frame);

         case Columns::Delta:
            return impl->frameDelta(frame, prev);

         case Columns::Rate:
            return impl->frameRate(frame);

         case Columns::Tech:
            return impl->frameTech(frame);

         case Columns::Event:
            return impl->frameEvent(frame, prev);

         case Columns::Flags:
            return impl->frameFlags(frame);

         case Columns::Data:
            return impl->frameData(frame);
      }

      return {};
   }

   else if (role == Qt::FontRole)
   {
      switch (index.column())
      {
         case Columns::Data:
         {
            if (frame->isPollFrame())
               return impl->requestDefaultFont;

            if (frame->isListenFrame())
               return impl->responseDefaultFont;

            break;
         }

         case Columns::Event:
         {
            if (frame->isListenFrame())
               return impl->responseDefaultFont;

            break;
         }
      }
   }

   else if (role == Qt::ForegroundRole)
   {
      switch (index.column())
      {
         case Columns::Event:
         case Columns::Data:
            if (frame->isListenFrame())
               return QColor(Qt::darkGray);
      }
   }

   else if (role == Qt::TextAlignmentRole)
   {
      switch (index.column())
      {
         case Columns::Id:
         case Columns::Time:
         case Columns::Delta:
            return Qt::AlignRight;

         case Columns::Rate:
            return Qt::AlignCenter;
      }

      return Qt::AlignLeft;
   }

   return {};
}

Qt::ItemFlags StreamModel::flags(const QModelIndex &index) const
{
   if (!index.isValid())
      return Qt::NoItemFlags;

   return {Qt::ItemIsEnabled | Qt::ItemIsSelectable};
}

QVariant StreamModel::headerData(int section, Qt::Orientation orientation, int role) const
{
   if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
      return impl->headers.value(section);

   return {};
}

QModelIndex StreamModel::index(int row, int column, const QModelIndex &parent) const
{
   if (!hasIndex(row, column, parent))
      return {};

   return createIndex(row, column, impl->frames[row]);
}

bool StreamModel::canFetchMore(const QModelIndex &parent) const
{
   QReadLocker locker(&impl->lock);

   return impl->stream.size() > 0;
}

void StreamModel::fetchMore(const QModelIndex &parent)
{
   QReadLocker locker(&impl->lock);

   beginInsertRows(QModelIndex(), impl->frames.size(), impl->frames.size() + impl->stream.size() - 1);

   while (!impl->stream.isEmpty())
   {
      impl->frames.append(new nfc::NfcFrame(impl->stream.dequeue()));
   }

   endInsertRows();
}

void StreamModel::resetModel()
{
   QWriteLocker locker(&impl->lock);

   beginResetModel();
   qDeleteAll(impl->frames);
   impl->frames.clear();
   endResetModel();
}

QModelIndexList StreamModel::modelRange(double from, double to)
{
   QModelIndexList list;

   for (int i = 0; i < impl->frames.size(); i++)
   {
      auto frame = impl->frames.at(i);

      if (frame->timeStart() >= from && frame->timeEnd() <= to)
      {
         list.append(index(i, 0));
      }
   }

   return list;
}

void StreamModel::append(const nfc::NfcFrame &frame)
{
   QWriteLocker locker(&impl->lock);

   impl->stream.enqueue(frame);
}

nfc::NfcFrame *StreamModel::frame(const QModelIndex &index) const
{
   if (!index.isValid())
      return nullptr;

   return static_cast<nfc::NfcFrame *>(index.internalPointer());
}

void StreamModel::setTimeFormat(int mode)
{
   impl->timeFormat = mode;

   this->modelChanged();
}

