/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <QEventLoop>

#include "RapidGptContext.h"
#include "RapigGptSettingsWindow.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace FOEDAG {

class RapidGptConnection : public QObject {
  Q_OBJECT

 public:
  explicit RapidGptConnection(const RapidGptSettings &settings);
  static QByteArray toByteArray(const RapidGptContext &context);

  bool send(const RapidGptContext &context);
  QString errorString() const;
  QString responseString() const;
  double delay() const;  // seconds

 private slots:
  void reply(QNetworkReply *r);

 private:
  QString url() const;

 private:
  RapidGptSettings m_settings{};
  QNetworkAccessManager *m_networkManager{nullptr};
  QEventLoop m_eventLoop;
  QString m_errorString{};
  QString m_response{};
  double m_delay{};
};

}  // namespace FOEDAG
