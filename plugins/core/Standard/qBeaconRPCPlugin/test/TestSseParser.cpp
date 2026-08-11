#include <QJsonObject>
#include <QObject>
#include <QTest>

#include "sseclient.h"

class TestSseParser : public QObject
{
	Q_OBJECT

private slots:
	void emptyBufferProducesNoEvents()
	{
		QByteArray buf;
		auto       events = SseClient::parseSseBuffer(buf);
		QVERIFY(events.isEmpty());
		QVERIFY(buf.isEmpty());
	}

	void commentLineIsIgnored()
	{
		QByteArray buf    = ": this is a comment\n";
		auto       events = SseClient::parseSseBuffer(buf);
		QVERIFY(events.isEmpty());
	}

	void eventLineWithoutDataPrefixIsIgnored()
	{
		QByteArray buf    = "event: message\n";
		auto       events = SseClient::parseSseBuffer(buf);
		QVERIFY(events.isEmpty());
	}

	void singleEventParsed()
	{
		QByteArray buf    = "data: {\"type\":\"send_to_cc\",\"method\":\"clear\"}\n";
		auto       events = SseClient::parseSseBuffer(buf);
		QCOMPARE(events.size(), 1);
		QCOMPARE(events[0]["type"].toString(), QString("send_to_cc"));
		QCOMPARE(events[0]["method"].toString(), QString("clear"));
	}

	void heartbeatIsFiltered()
	{
		QByteArray buf    = "data: {\"type\":\"heartbeat\",\"trace_id\":\"abc\"}\n";
		auto       events = SseClient::parseSseBuffer(buf);
		QVERIFY(events.isEmpty());
	}

	void heartbeatAmongRealEventsFiltered()
	{
		QByteArray buf =
			"data: {\"type\":\"heartbeat\"}\n"
			"data: {\"type\":\"send_to_cc\",\"method\":\"version\"}\n"
			"data: {\"type\":\"heartbeat\"}\n";
		auto events = SseClient::parseSseBuffer(buf);
		QCOMPARE(events.size(), 1);
		QCOMPARE(events[0]["method"].toString(), QString("version"));
	}

	void multipleEventsAllParsed()
	{
		QByteArray buf =
			"data: {\"type\":\"send_to_cc\",\"method\":\"clear\"}\n"
			"data: {\"type\":\"send_to_cc\",\"method\":\"version\"}\n";
		auto events = SseClient::parseSseBuffer(buf);
		QCOMPARE(events.size(), 2);
		QCOMPARE(events[0]["method"].toString(), QString("clear"));
		QCOMPARE(events[1]["method"].toString(), QString("version"));
	}

	void incompleteLineLeftInBuffer()
	{
		QByteArray buf    = "data: {\"type\":\"send_to_cc\"}";
		auto       events = SseClient::parseSseBuffer(buf);
		QVERIFY(events.isEmpty());
		QVERIFY(!buf.isEmpty());
	}

	void twoChunksMakeOneEvent()
	{
		QByteArray buf    = "data: {\"type\":\"send_to_c";
		auto       events = SseClient::parseSseBuffer(buf);
		QVERIFY(events.isEmpty());

		buf += "c\",\"method\":\"clear\"}\n";
		events = SseClient::parseSseBuffer(buf);
		QCOMPARE(events.size(), 1);
		QCOMPARE(events[0]["method"].toString(), QString("clear"));
	}

	void paramsAreParsedCorrectly()
	{
		QByteArray buf =
			"data: {\"type\":\"send_to_cc\",\"method\":\"open\","
			"\"params\":{\"filename\":\"/tmp/scan.ply\",\"silent\":true}}\n";
		auto events = SseClient::parseSseBuffer(buf);
		QCOMPARE(events.size(), 1);

		QJsonObject params = events[0]["params"].toObject();
		QCOMPARE(params["filename"].toString(), QString("/tmp/scan.ply"));
		QVERIFY(params["silent"].toBool());
	}

	void malformedJsonIsSkipped()
	{
		QByteArray buf =
			"data: not-valid-json\n"
			"data: {\"type\":\"send_to_cc\",\"method\":\"clear\"}\n";
		auto events = SseClient::parseSseBuffer(buf);
		QCOMPARE(events.size(), 1);
		QCOMPARE(events[0]["method"].toString(), QString("clear"));
	}

	void emptyDataLineIsSkipped()
	{
		QByteArray buf    = "data: \n";
		auto       events = SseClient::parseSseBuffer(buf);
		QVERIFY(events.isEmpty());
	}

	void backoffDoublesEachStep()
	{
		double           backoff    = 1.0;
		constexpr double maxBackoff = 30.0;

		QList<double> expected = {2.0, 4.0, 8.0, 16.0, 30.0, 30.0};
		for (double exp : expected)
		{
			backoff = std::min(backoff * 2.0, maxBackoff);
			QCOMPARE(backoff, exp);
		}
	}
};

QTEST_GUILESS_MAIN(TestSseParser)
#include "TestSseParser.moc"
