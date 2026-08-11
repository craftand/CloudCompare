#include "jsonrpcclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

JsonRPCClient::JsonRPCClient(QObject* parent)
	: QObject(parent)
{
}

void JsonRPCClient::setConfig(const OrchestratorConfig& cfg)
{
	m_config = cfg;
}

void JsonRPCClient::call(const QString& method, const QJsonObject& params)
{
	call(method, params, nullptr);
}

void JsonRPCClient::call(const QString&                     method,
						  const QJsonObject&                 params,
						  std::function<void(JsonRPCResult)> callback)
{
	int            id    = m_nextId++;
	QNetworkReply* reply = sendRequest(method, params, id);
	if (!reply)
		return;

	if (callback)
		m_callbacks[reply] = std::move(callback);

	connect(reply, &QNetworkReply::finished, this, &JsonRPCClient::onReplyFinished);
}

QNetworkReply* JsonRPCClient::sendRequest(const QString&    method,
										   const QJsonObject& params,
										   int                id)
{
	QUrl            url(m_config.orchestratorUrl + "/rpc");
	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	if (!m_config.localToken.isEmpty())
		req.setRawHeader("X-Local-Token", m_config.localToken.toUtf8());
	if (!m_traceId.isEmpty())
		req.setRawHeader("X-Trace-ID", m_traceId.toUtf8());

	QJsonObject body;
	body["jsonrpc"] = "2.0";
	body["method"]  = method;
	body["params"]  = params;
	body["id"]      = id;

	QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
	return m_nam.post(req, data);
}

void JsonRPCClient::onReplyFinished()
{
	auto* reply = qobject_cast<QNetworkReply*>(sender());
	if (!reply)
		return;

	auto cb = m_callbacks.take(reply);

	if (reply->error() != QNetworkReply::NoError)
	{
		qWarning() << "[BeaconRPC Client] Network error:" << reply->errorString();
		if (cb)
			cb(JsonRPCResult::error(-32000, reply->errorString()));
		Q_EMIT connectionError(reply->errorString());
		reply->deleteLater();
		return;
	}

	QByteArray      raw = reply->readAll();
	QJsonParseError err;
	QJsonDocument   doc = QJsonDocument::fromJson(raw, &err);
	reply->deleteLater();

	if (err.error != QJsonParseError::NoError || !doc.isObject())
	{
		if (cb)
			cb(JsonRPCResult::error(-32700, "Parse error"));
		return;
	}

	QJsonObject   obj = doc.object();
	JsonRPCResult result;

	if (obj.contains("error"))
	{
		QJsonObject e = obj["error"].toObject();
		result        = JsonRPCResult::error(e["code"].toInt(-32603),
											 e["message"].toString("Internal error"));
	}
	else
	{
		result = JsonRPCResult::success(obj["result"].toVariant());
	}

	if (cb)
		cb(result);
}

void JsonRPCClient::setTraceId(const QString& id)
{
	m_traceId = id;
}
