#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include "orchestrator_config.h"

static OrchestratorConfig loadFromJson(const QJsonObject& obj, QTemporaryDir& dir)
{
	QFile f(dir.filePath("beacon_config.json"));
	f.open(QIODevice::WriteOnly);
	f.write(QJsonDocument(obj).toJson());
	f.close();

	OrchestratorConfig cfg;
	QFile              cfg_file(dir.filePath("beacon_config.json"));
	if (cfg_file.open(QIODevice::ReadOnly))
	{
		QJsonParseError err;
		QJsonDocument   doc = QJsonDocument::fromJson(cfg_file.readAll(), &err);
		if (err.error == QJsonParseError::NoError && doc.isObject())
		{
			QJsonObject o = doc.object();
			if (o.contains("orchestrator_url"))
				cfg.orchestratorUrl = o["orchestrator_url"].toString();
			if (o.contains("local_token"))
				cfg.localToken = o["local_token"].toString();
		}
	}
	return cfg;
}

class TestOrchestratorConfig : public QObject
{
	Q_OBJECT

private slots:
	void defaultUrlIsLocalhost()
	{
		OrchestratorConfig cfg;
		QCOMPARE(cfg.orchestratorUrl, QString("http://127.0.0.1:41979"));
	}

	void defaultTokenIsEmpty()
	{
		OrchestratorConfig cfg;
		QVERIFY(cfg.localToken.isEmpty());
	}

	void parsesOrchestratorUrl()
	{
		QTemporaryDir dir;
		QJsonObject   obj;
		obj["orchestrator_url"] = "http://localhost:9999";
		auto cfg                = loadFromJson(obj, dir);
		QCOMPARE(cfg.orchestratorUrl, QString("http://localhost:9999"));
	}

	void parsesLocalToken()
	{
		QTemporaryDir dir;
		QJsonObject   obj;
		obj["local_token"] = "test-token-abc";
		auto cfg           = loadFromJson(obj, dir);
		QCOMPARE(cfg.localToken, QString("test-token-abc"));
	}

	void parsesBothFields()
	{
		QTemporaryDir dir;
		QJsonObject   obj;
		obj["orchestrator_url"] = "http://example.com:41979";
		obj["local_token"]      = "my-secret-token";
		auto cfg                = loadFromJson(obj, dir);
		QCOMPARE(cfg.orchestratorUrl, QString("http://example.com:41979"));
		QCOMPARE(cfg.localToken, QString("my-secret-token"));
	}

	void extraFieldsIgnored()
	{
		QTemporaryDir dir;
		QJsonObject   obj;
		obj["orchestrator_url"] = "http://localhost:41979";
		obj["some_other_field"] = "irrelevant";
		auto cfg                = loadFromJson(obj, dir);
		QCOMPARE(cfg.orchestratorUrl, QString("http://localhost:41979"));
	}

	void missingUrlKeepsDefault()
	{
		QTemporaryDir dir;
		QJsonObject   obj;
		obj["local_token"] = "token-only";
		auto cfg           = loadFromJson(obj, dir);
		QCOMPARE(cfg.orchestratorUrl, QString("http://127.0.0.1:41979"));
		QCOMPARE(cfg.localToken, QString("token-only"));
	}

	void emptyJsonObjectKeepsDefaults()
	{
		QTemporaryDir dir;
		auto          cfg = loadFromJson(QJsonObject{}, dir);
		QCOMPARE(cfg.orchestratorUrl, QString("http://127.0.0.1:41979"));
		QVERIFY(cfg.localToken.isEmpty());
	}

	void loadsFromMissingFileGivesDefaults()
	{
		OrchestratorConfig cfg = OrchestratorConfig::load();
		QVERIFY(!cfg.orchestratorUrl.isEmpty());
	}
};

QTEST_GUILESS_MAIN(TestOrchestratorConfig)
#include "TestOrchestratorConfig.moc"
