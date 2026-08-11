#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include "jsonrpcclient.h"

class MockHttpServer : public QObject
{
	Q_OBJECT
public:
	explicit MockHttpServer(QObject* parent = nullptr) : QObject(parent)
	{
		m_server = new QTcpServer(this);
		m_server->listen(QHostAddress::LocalHost, 0);
		connect(m_server, &QTcpServer::newConnection,
				this,      &MockHttpServer::onNewConnection);
	}

	quint16 port() const { return m_server->serverPort(); }

	void setResponse(const QByteArray& httpResponse)
	{
		m_nextResponse = httpResponse;
	}

	QByteArray lastRequest() const { return m_lastRequest; }

	static QByteArray jsonResponse(const QJsonObject& body)
	{
		QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
		return "HTTP/1.1 200 OK\r\n"
			   "Content-Type: application/json\r\n"
			   "Content-Length: " +
			   QByteArray::number(json.size()) +
			   "\r\n"
			   "\r\n" +
			   json;
	}

private slots:
	void onNewConnection()
	{
		QTcpSocket* socket = m_server->nextPendingConnection();
		connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
			m_lastRequest += socket->readAll();
			if (m_lastRequest.contains("\r\n\r\n"))
			{
				socket->write(m_nextResponse);
				socket->flush();
				socket->disconnectFromHost();
			}
		});
	}

private:
	QTcpServer* m_server;
	QByteArray  m_nextResponse;
	QByteArray  m_lastRequest;
};

class TestJsonRPCClient : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		qputenv("QT_BEACON_SKIP_KEYSTORE", "1");
	}

private:
	OrchestratorConfig configForServer(const MockHttpServer& srv,
									   const QString&        token = "test-token")
	{
		OrchestratorConfig cfg;
		cfg.orchestratorUrl = QString("http://127.0.0.1:%1").arg(srv.port());
		cfg.localToken      = token;
		return cfg;
	}

private slots:
	void successCallbackReceivesResult()
	{
		MockHttpServer srv;
		QJsonObject    responseBody;
		responseBody["jsonrpc"] = "2.0";
		responseBody["result"]  = 42;
		responseBody["id"]      = 1;
		srv.setResponse(MockHttpServer::jsonResponse(responseBody));

		JsonRPCClient client;
		client.setConfig(configForServer(srv));

		JsonRPCResult received;
		bool          called = false;
		client.call("version", {}, [&](JsonRPCResult r) {
			received = r;
			called   = true;
		});

		QTRY_VERIFY_WITH_TIMEOUT(called, 2000);
		QVERIFY(!received.isError);
		QCOMPARE(received.result.toInt(), 42);
	}

	void errorResponseCallbackReceivesError()
	{
		MockHttpServer srv;
		QJsonObject    errorObj;
		errorObj["code"]    = -32601;
		errorObj["message"] = "Method not found";
		QJsonObject responseBody;
		responseBody["jsonrpc"] = "2.0";
		responseBody["error"]   = errorObj;
		responseBody["id"]      = 1;
		srv.setResponse(MockHttpServer::jsonResponse(responseBody));

		JsonRPCClient client;
		client.setConfig(configForServer(srv));

		JsonRPCResult received;
		bool          called = false;
		client.call("unknown_method", {}, [&](JsonRPCResult r) {
			received = r;
			called   = true;
		});

		QTRY_VERIFY_WITH_TIMEOUT(called, 2000);
		QVERIFY(received.isError);
		QCOMPARE(received.error_code, -32601);
		QCOMPARE(received.error_message, QString("Method not found"));
	}

	void requestContainsJsonrpcVersion()
	{
		MockHttpServer srv;
		QJsonObject    ok;
		ok["jsonrpc"] = "2.0";
		ok["result"]  = 0;
		ok["id"]      = 1;
		srv.setResponse(MockHttpServer::jsonResponse(ok));

		JsonRPCClient client;
		client.setConfig(configForServer(srv));
		client.call("handshake", {}, nullptr);

		QTRY_VERIFY_WITH_TIMEOUT(!srv.lastRequest().isEmpty(), 2000);

		QByteArray  req  = srv.lastRequest();
		int         sep  = req.indexOf("\r\n\r\n");
		QByteArray  body = req.mid(sep + 4);
		QJsonObject sent = QJsonDocument::fromJson(body).object();

		QCOMPARE(sent["jsonrpc"].toString(), QString("2.0"));
		QCOMPARE(sent["method"].toString(), QString("handshake"));
	}

	void requestContainsLocalTokenHeader()
	{
		MockHttpServer srv;
		QJsonObject    ok;
		ok["jsonrpc"] = "2.0";
		ok["result"]  = 0;
		ok["id"]      = 1;
		srv.setResponse(MockHttpServer::jsonResponse(ok));

		JsonRPCClient client;
		client.setConfig(configForServer(srv, "my-secret-token"));
		client.call("handshake", {}, nullptr);

		QTRY_VERIFY_WITH_TIMEOUT(!srv.lastRequest().isEmpty(), 2000);
		QByteArray req = srv.lastRequest();
		QVERIFY2(req.contains("x-local-token: my-secret-token") || req.contains("X-Local-Token: my-secret-token"),
		         req.constData());
	}

	void requestContainsTraceIdHeaderAfterSet()
	{
		MockHttpServer srv;
		QJsonObject    ok;
		ok["jsonrpc"] = "2.0";
		ok["result"]  = 0;
		ok["id"]      = 1;
		srv.setResponse(MockHttpServer::jsonResponse(ok));

		JsonRPCClient client;
		client.setConfig(configForServer(srv));
		client.setTraceId("trace-xyz-123");
		client.call("handshake", {}, nullptr);

		QTRY_VERIFY_WITH_TIMEOUT(!srv.lastRequest().isEmpty(), 2000);
		QByteArray req = srv.lastRequest();
		QVERIFY2(req.contains("x-trace-id: trace-xyz-123") || req.contains("X-Trace-ID: trace-xyz-123"),
		         req.constData());
	}

	void networkErrorCallbackReceivesError()
	{
		OrchestratorConfig cfg;
		cfg.orchestratorUrl = "http://127.0.0.1:1";
		cfg.localToken      = "";

		JsonRPCClient client;
		client.setConfig(cfg);

		JsonRPCResult received;
		bool          called = false;
		client.call("version", {}, [&](JsonRPCResult r) {
			received = r;
			called   = true;
		});

		QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
		QVERIFY(received.isError);
	}

	void fireAndForgetDoesNotCrash()
	{
		MockHttpServer srv;
		QJsonObject    ok;
		ok["jsonrpc"] = "2.0";
		ok["result"]  = 0;
		ok["id"]      = 1;
		srv.setResponse(MockHttpServer::jsonResponse(ok));

		JsonRPCClient client;
		client.setConfig(configForServer(srv));
		client.call("log_trace", {{"level", "INFO"}, {"message", "test"}});

		QTest::qWait(500);
	}
};

QTEST_MAIN(TestJsonRPCClient)
#include "TestJsonRPCClient.moc"
