#pragma once

#include <functional>
#include <QHash>
#include <QJsonObject>
#include "ccStdPluginInterface.h"
#include "jsonrpcclient.h"
#include "sseclient.h"
#include "orchestrator_config.h"

class BeaconRPCPlugin : public QObject, public ccStdPluginInterface
{
	Q_OBJECT
	Q_INTERFACES(ccPluginInterface ccStdPluginInterface)
	Q_PLUGIN_METADATA(IID "cccorp.cloudcompare.plugin.BeaconRPC" FILE "../info.json")

public:
	explicit BeaconRPCPlugin(QObject* parent = nullptr);
	~BeaconRPCPlugin() override = default;

	QList<QAction*> getActions() override;

public slots:
	void triggered(bool checked);
	void onSseEvent(QJsonObject payload);

private:
	using MethodHandler = std::function<JsonRPCResult(const QMap<QString, QVariant>&)>;

	void registerMethods();
	void bootstrapAndHandshake();
	JsonRPCResult executeCommand(const QString&               method,
								 const QMap<QString, QVariant>& params);

	// Dedicated Method Handlers
	JsonRPCResult handleOpen(const QMap<QString, QVariant>& params);
	JsonRPCResult handleCompareScans(const QMap<QString, QVariant>& params);
	JsonRPCResult handleComputeDistance(const QMap<QString, QVariant>& params);
	JsonRPCResult handleCcToFusion(const QMap<QString, QVariant>& params);
	JsonRPCResult handleClear(const QMap<QString, QVariant>& params);
	JsonRPCResult handleVersion(const QMap<QString, QVariant>& params);

	// Console-first Logging Helpers
	void logInfo(const QString& msg);
	void logWarning(const QString& msg);
	void logError(const QString& msg);

	QString     recursiveName(ccHObject*);
	ccHObject*  createParent(QString path);

	QAction*           m_action{nullptr};
	const QString      m_version{"1.2"};

	OrchestratorConfig m_config;
	JsonRPCClient      m_rpcClient;
	SseClient          m_sseClient;
	QString            m_traceId;

	QHash<QString, MethodHandler> m_methodHandlers;
};
