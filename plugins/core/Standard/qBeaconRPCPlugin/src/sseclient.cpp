#include "sseclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkRequest>
#include <QUrl>

SseClient::SseClient(QObject* parent)
	: QObject(parent)
{
	m_reconnectTimer.setSingleShot(true);
	connect(&m_reconnectTimer, &QTimer::timeout, this, &SseClient::doConnect);
}

SseClient::~SseClient()
{
	stop();
}

void SseClient::setConfig(const OrchestratorConfig& cfg)
{
	m_config = cfg;
}

void SseClient::start()
{
	m_stopping = false;
	m_backoff  = 1.0;
	doConnect();
}

void SseClient::stop()
{
	m_stopping = true;
	m_reconnectTimer.stop();

	if (m_reply)
	{
		m_reply->abort();
		m_reply->deleteLater();
		m_reply = nullptr;
	}

	m_buffer.clear();
	Q_EMIT disconnected();
}

bool SseClient::isConnected() const
{
	return m_reply && m_reply->isRunning();
}

void SseClient::doConnect()
{
	if (m_stopping)
		return;

	if (m_reply)
	{
		m_reply->abort();
		m_reply->deleteLater();
		m_reply = nullptr;
	}
	m_buffer.clear();

	QUrl            url(m_config.orchestratorUrl + "/stream");
	QNetworkRequest req(url);
	req.setRawHeader("Accept", "text/event-stream");
	req.setRawHeader("Cache-Control", "no-cache");
	if (!m_config.localToken.isEmpty())
		req.setRawHeader("X-Local-Token", m_config.localToken.toUtf8());

	req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
					  QNetworkRequest::AlwaysNetwork);

	qDebug() << "[BeaconRPC SSE] Connecting to" << url.toString();
	m_reply = m_nam.get(req);

	connect(m_reply, &QNetworkReply::readyRead, this, &SseClient::onReadyRead);
	connect(m_reply, &QNetworkReply::finished, this, &SseClient::onFinished);

	Q_EMIT connected();
}

void SseClient::scheduleReconnect()
{
	if (m_stopping)
		return;

	qDebug() << "[BeaconRPC SSE] Reconnecting in" << m_backoff << "s";
	m_reconnectTimer.start(static_cast<int>(m_backoff * 1000));
	m_backoff = std::min(m_backoff * 2.0, k_maxBackoff);
}

QList<QJsonObject> SseClient::parseSseBuffer(QByteArray& buffer)
{
	QList<QJsonObject> events;

	while (true)
	{
		int newlinePos = buffer.indexOf('\n');
		if (newlinePos < 0)
			break;

		QByteArray line = buffer.left(newlinePos).trimmed();
		buffer          = buffer.mid(newlinePos + 1);

		if (!line.startsWith("data: "))
			continue;

		QByteArray      dataStr = line.mid(6);
		QJsonParseError parseErr;
		QJsonDocument   doc = QJsonDocument::fromJson(dataStr, &parseErr);
		if (parseErr.error != QJsonParseError::NoError || !doc.isObject())
		{
			qWarning() << "[BeaconRPC SSE] JSON parse error:" << parseErr.errorString();
			continue;
		}

		QJsonObject payload = doc.object();
		if (payload["type"].toString() == "heartbeat")
			continue;

		events.append(payload);
	}

	return events;
}

void SseClient::onReadyRead()
{
	if (!m_reply)
		return;

	m_buffer += m_reply->readAll();

	const auto events = parseSseBuffer(m_buffer);
	for (const QJsonObject& payload : events)
	{
		qDebug() << "[BeaconRPC SSE] Event received:" << payload["type"].toString();
		Q_EMIT eventReceived(payload);
	}
}

void SseClient::onFinished()
{
	if (!m_reply)
		return;

	QNetworkReply::NetworkError err = m_reply->error();
	if (err != QNetworkReply::NoError && err != QNetworkReply::OperationCanceledError)
	{
		QString msg = m_reply->errorString();
		qWarning() << "[BeaconRPC SSE] Connection failed:" << msg;
		Q_EMIT errorOccurred(msg);
	}

	m_reply->deleteLater();
	m_reply = nullptr;
	m_buffer.clear();

	Q_EMIT disconnected();
	scheduleReconnect();
}
