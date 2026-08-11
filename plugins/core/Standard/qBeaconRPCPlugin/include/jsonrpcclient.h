#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <functional>

#include "orchestrator_config.h"

struct JsonRPCResult
{
	static JsonRPCResult error(int code, QString message)
	{
		JsonRPCResult result;
		result.isError       = true;
		result.error_code    = code;
		result.error_message = message;
		return result;
	}

	static JsonRPCResult success(QVariant value)
	{
		JsonRPCResult result;
		result.isError = false;
		result.result  = value;
		return result;
	}

	bool     isError{true};
	int      error_code{-32601};
	QString  error_message{"Method not found"};
	QVariant result;
};

class JsonRPCClient : public QObject
{
	Q_OBJECT
public:
	explicit JsonRPCClient(QObject* parent = nullptr);

	void setConfig(const OrchestratorConfig& cfg);
	void call(const QString& method, const QJsonObject& params);
	void call(const QString&              method,
			  const QJsonObject&           params,
			  std::function<void(JsonRPCResult)> callback);
	void setTraceId(const QString& id);

Q_SIGNALS:
	void connectionError(QString message);

private slots:
	void onReplyFinished();

private:
	QNetworkAccessManager              m_nam;
	OrchestratorConfig                 m_config;
	int                                m_nextId{1};
	QString                            m_traceId;
	QMap<QNetworkReply*, std::function<void(JsonRPCResult)>> m_callbacks;

	QNetworkReply* sendRequest(const QString&    method,
							   const QJsonObject& params,
							   int                id);
};
