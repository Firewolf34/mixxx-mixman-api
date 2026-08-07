/* This file is part of Clementine.
   Copyright 2010, David Sansome <me@davidsansome.com>

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MOCK_NETWORKACCESSMANAGER_H
#define MOCK_NETWORKACCESSMANAGER_H

#include <QByteArray>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringList>
#include <QUrl>

#include "gmock/gmock.h"

// Usage:
// Create a MockNetworkAccessManager.
// Call ExpectGet() with appropriate expectations and the data you want back.
// This will return a MockNetworkReply*. When you are ready for the reply to
// arrive, call MockNetworkReply::Done().

class MockNetworkReply : public QNetworkReply {
    Q_OBJECT
  public:
    MockNetworkReply(const QByteArray& data = nullptr);

    // Use these to set expectations.
    void SetData(const QByteArray& data);
    void SetRequest(const QNetworkRequest& request);
    virtual void setAttribute(QNetworkRequest::Attribute code, const QVariant& value);

    // Call this when you are ready for the reply signals.
    void Done(bool emitReadyRead = false);
    bool WasAborted() const {
        return m_aborted;
    }

  protected:
    void abort() override;
    qint64 readData(char* data, qint64 len) override;
    qint64 writeData(const char* data, qint64 len) override;

    QByteArray m_data;
    qint64 m_pos;
    bool m_aborted = false;
};

class MockNetworkAccessManager : public QNetworkAccessManager {
    Q_OBJECT
  public:
    MockNetworkReply* ExpectGet(
            const QString& contains,              // A string that should be present in the URL.
            const QMap<QString, QString>& params, // Required URL parameters.
            int status,                           // Returned HTTP status code.
            const QByteArray& ret_data);          // Returned data.
    MockNetworkReply* ExpectPost(
            const QString& contains,
            const QMap<QString, QString>& params,
            const QStringList& body_contains,
            int status,
            const QByteArray& ret_data);
    MockNetworkReply* ExpectPut(
            const QString& contains,
            const QMap<QString, QString>& params,
            const QStringList& body_contains,
            int status,
            const QByteArray& ret_data);
  protected:
    MOCK_METHOD3(createRequest, QNetworkReply*(Operation, const QNetworkRequest&, QIODevice*));
};

#endif
