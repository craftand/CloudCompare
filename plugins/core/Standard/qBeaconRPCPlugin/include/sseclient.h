#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QTimer>
#include <cmath>

#include "orchestrator_config.h"

class SseClient : public QObject
{
	Q_OBJECT
public:
	explicit SseClient(QObject* parent = nullptr);
	~SseClient() override;

	void setConfig(const OrchestratorConfig& cfg);
	void start();
	void stop();
	bool isConnected() const;

	static QList<QJsonObject> parseSseBuffer(QByteArray& buffer);

Q_SIGNALS:
	void eventReceived(QJsonObject payload);
	void connected();
	void disconnected();
	void errorOccurred(QString error);

private slots:
	void onReadyRead();
	void onFinished();
	void doConnect();

private:
	OrchestratorConfig    m_config;
	QNetworkAccessManager m_nam;
	QNetworkReply*        m_reply{nullptr};
	QByteArray            m_buffer;
	QTimer                m_reconnectTimer;
	double                m_backoff{1.0};
	static constexpr double k_maxBackoff = 30.0;
	bool                  m_stopping{false};

	void scheduleReconnect();
};
