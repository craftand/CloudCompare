#include <QObject>
#include <QTest>

#include "jsonrpcclient.h"

class TestJsonRPCResult : public QObject
{
	Q_OBJECT

private slots:
	void defaultIsError()
	{
		JsonRPCResult r;
		QVERIFY(r.isError);
	}

	void defaultErrorCodeIsMethodNotFound()
	{
		JsonRPCResult r;
		QCOMPARE(r.error_code, -32601);
	}

	void errorFactoryIsError()
	{
		auto r = JsonRPCResult::error(-32600, "Invalid Request");
		QVERIFY(r.isError);
	}

	void errorFactoryCode()
	{
		auto r = JsonRPCResult::error(-32600, "Invalid Request");
		QCOMPARE(r.error_code, -32600);
	}

	void errorFactoryMessage()
	{
		auto r = JsonRPCResult::error(-32600, "Invalid Request");
		QCOMPARE(r.error_message, QString("Invalid Request"));
	}

	void errorFactoryEmptyMessage()
	{
		auto r = JsonRPCResult::error(1, "");
		QVERIFY(r.isError);
		QVERIFY(r.error_message.isEmpty());
	}

	void successFactoryIsNotError()
	{
		auto r = JsonRPCResult::success(42);
		QVERIFY(!r.isError);
	}

	void successFactoryIntResult()
	{
		auto r = JsonRPCResult::success(42);
		QCOMPARE(r.result.toInt(), 42);
	}

	void successFactoryStringResult()
	{
		auto r = JsonRPCResult::success(QString("2.0"));
		QCOMPARE(r.result.toString(), QString("2.0"));
	}

	void successFactoryZero()
	{
		auto r = JsonRPCResult::success(0);
		QVERIFY(!r.isError);
		QCOMPARE(r.result.toInt(), 0);
	}

	void successFactoryBoolResult()
	{
		auto r = JsonRPCResult::success(true);
		QVERIFY(!r.isError);
		QVERIFY(r.result.toBool());
	}
};

QTEST_GUILESS_MAIN(TestJsonRPCResult)
#include "TestJsonRPCResult.moc"
