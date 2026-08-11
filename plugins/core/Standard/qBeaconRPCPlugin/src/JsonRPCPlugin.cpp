//##########################################################################
//#                                                                        #
//#                CLOUDCOMPARE PLUGIN: BeaconRPCPlugin                    #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#                             COPYRIGHT: theAdib, 2020                   #
//#                                                                        #
//##########################################################################

// Qt
#include <QtGui>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>

// local
#include "JsonRPCPlugin.h"

// qCC_db
#include <ccGenericPointCloud.h>
// qCC_gl
#include <ccGLWindowInterface.h>
// qCC_io
#include <FileIOFilter.h>
// CC plugins
#include <ccMainAppInterface.h>

BeaconRPCPlugin::BeaconRPCPlugin(QObject* parent)
	: QObject(parent)
	, ccStdPluginInterface(":/CC/plugin/BeaconRPCPlugin/info.json")
{
	qDebug() << "BeaconRPCPlugin::BeaconRPCPlugin";

	m_config = OrchestratorConfig::load();
	m_rpcClient.setConfig(m_config);
	m_sseClient.setConfig(m_config);

	connect(&m_sseClient, &SseClient::eventReceived,
			this,         &BeaconRPCPlugin::onSseEvent);

	connect(&m_sseClient, &SseClient::errorOccurred, this,
			[](const QString& err) {
				qWarning() << "[BeaconRPC] SSE error:" << err;
			});
}

QList<QAction*> BeaconRPCPlugin::getActions()
{
	qDebug() << "BeaconRPCPlugin::getActions";

	if (!m_action)
	{
		m_action = new QAction(getName(), this);
		m_action->setToolTip(getDescription());
		m_action->setIcon(getIcon());
		m_action->setCheckable(true);
		m_action->setChecked(true);
		m_action->setEnabled(true);

		connect(m_action, &QAction::triggered, this, &BeaconRPCPlugin::triggered);
	}

	return {m_action};
}

void BeaconRPCPlugin::triggered(bool checked)
{
	qDebug() << "BeaconRPCPlugin::triggered" << checked;

	if (checked)
	{
		bootstrapAndHandshake();
		m_sseClient.start();
	}
	else
	{
		m_sseClient.stop();
	}
}

void BeaconRPCPlugin::bootstrapAndHandshake()
{
	QUrl            traceUrl(m_config.orchestratorUrl + "/trace/start");
	QNetworkRequest req(traceUrl);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	if (!m_config.localToken.isEmpty())
	{
		req.setRawHeader("X-Local-Token", m_config.localToken.toUtf8());
	}

	auto* bootNam = new QNetworkAccessManager(this);
	auto* reply   = bootNam->post(req, QByteArray("{}"));

	connect(reply, &QNetworkReply::finished, this, [this, reply, bootNam]() {
		if (reply->error() == QNetworkReply::NoError)
		{
			QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
			m_traceId         = doc.object()["trace_id"].toString();
			m_rpcClient.setTraceId(m_traceId);
			qDebug() << "[BeaconRPC] Connected to orchestrator. Trace ID:" << m_traceId;
		}
		else
		{
			m_traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
			m_rpcClient.setTraceId(m_traceId);
			qWarning() << "[BeaconRPC] Orchestrator unreachable, running offline. Trace:"
					   << m_traceId;
		}
		reply->deleteLater();
		bootNam->deleteLater();

		m_rpcClient.call("handshake", {}, [](JsonRPCResult r) {
			if (!r.isError)
				qDebug() << "[BeaconRPC] Handshake OK:" << r.result.toString();
			else
				qWarning() << "[BeaconRPC] Handshake failed:" << r.error_message;
		});
	});
}

void BeaconRPCPlugin::onSseEvent(QJsonObject payload)
{
	QString type = payload["type"].toString();
	qDebug() << "[BeaconRPC] SSE event type:" << type;

	if (type != "send_to_cc")
		return;

	QString method = payload["method"].toString();
	auto    params = payload["params"].toVariant().toMap();

	if (method.isEmpty())
	{
		qWarning() << "[BeaconRPC] send_to_cc event missing 'method' field";
		return;
	}

	JsonRPCResult result = executeCommand(method, params);

	QJsonObject logParams;
	logParams["level"]       = result.isError ? "ERROR" : "INFO";
	logParams["message"]     = result.isError
								   ? QString("Command '%1' failed: %2").arg(method, result.error_message)
								   : QString("Command '%1' completed successfully").arg(method);
	logParams["filename"]    = "JsonRPCPlugin.cpp";
	logParams["line_number"] = 0;
	m_rpcClient.call("log_trace", logParams);
}

JsonRPCResult BeaconRPCPlugin::executeCommand(const QString&               method,
											  const QMap<QString, QVariant>& params)
{
	qDebug() << "[BeaconRPC] executeCommand:" << method << params;

	if (m_app == nullptr)
	{
		return JsonRPCResult::error(-32603, "CloudCompare app interface not available");
	}

	JsonRPCResult result;
	bool          need_redraw = false;

	if (method == "open")
	{
		QString filename = params["filename"].toString();

		CCVector3d loadCoordinatesShift(0, 0, 0);
		bool       loadCoordinatesTransEnabled = false;
		bool       loadCoordinatesTransForced  = false;

		FileIOFilter::LoadParameters parameters;
		parameters.alwaysDisplayLoadDialog   = true;
		parameters.shiftHandlingMode         = ccGlobalShiftManager::DIALOG_IF_NECESSARY;
		parameters._coordinatesShift         = &loadCoordinatesShift;
		parameters._coordinatesShiftEnabled  = &loadCoordinatesTransEnabled;
		parameters._coordinatesShiftForced   = &loadCoordinatesTransForced;
		parameters.parentWidget              = m_app->getActiveGLWindow()->asWidget();

		if (params.contains("silent") && (params["silent"].toBool() == true))
		{
			parameters.alwaysDisplayLoadDialog = false;
		}

		CC_FILE_ERROR res      = CC_FERR_NO_ERROR;
		ccHObject*    newGroup = FileIOFilter::LoadFromFile(filename, parameters, res,
															 params["filter"].toString());

		if (newGroup)
		{
			ccHObject::Container clouds;
			newGroup->filterChildren(clouds, true, CC_TYPES::POINT_CLOUD);
			for (ccHObject* cloud : clouds)
			{
				if (cloud)
				{
					static_cast<ccGenericPointCloud*>(cloud)->showNormals(false);
				}
			}

			QList<QVariant> transformation = params["transformation"].toList();
			if (transformation.size() == 4 * 4)
			{
				std::vector<double> values(4 * 4);
				bool                success = true;
				for (unsigned i = 0; i < 4 * 4; ++i)
				{
					double d = transformation[i].toDouble(&success);
					if (!success)
					{
						break;
					}
					values[((i % 4) * 4) + (i / 4)] = d;
				}
				if (success)
				{
					ccGLMatrix mat = ccGLMatrix(values.data());
					newGroup->setGLTransformation(mat);
				}
			}

			QString objname = params["name"].toString().trimmed();
			if (!objname.isEmpty())
			{
				newGroup->setName(objname);
			}

			QString parentname = params["parent"].toString().trimmed();
			if (!parentname.isEmpty())
			{
				auto parent = createParent(parentname);
				if (parent)
				{
					parent->addChild(newGroup);
					const auto display = m_app->getActiveGLWindow();
					if (display)
					{
						newGroup->setDisplay_recursive(display);
					}
				}
			}

			m_app->addToDB(newGroup);

			auto display = m_app->getActiveGLWindow();
			if (display)
			{
				display->zoomGlobal();
			}

			need_redraw = true;
			result      = JsonRPCResult::success(0);
		}
		else
		{
			result = JsonRPCResult::error(1, "cancelled by user");
		}
	}
	else if (method == "clear")
	{
		auto       root = m_app->dbRootObject();
		ccHObject* child;
		while ((child = root->getChild(0)) != nullptr)
		{
			m_app->removeFromDB(child, true);
		}
		need_redraw = true;
		result      = JsonRPCResult::success(0);
	}
	else if (method == "version")
	{
		result = JsonRPCResult::success(m_version);
	}
	else
	{
		result = JsonRPCResult::error(-32601, "Method not found: " + method);
	}

	if (need_redraw)
	{
		ccGLWindowInterface* win = m_app->getActiveGLWindow();
		if (win)
			win->redraw();
	}

	return result;
}

QString BeaconRPCPlugin::recursiveName(ccHObject* obj)
{
	QString name;

	while (obj)
	{
		name.prepend(obj->getName() + "/");
		obj = obj->getParent();
	}

	return name;
}

ccHObject* BeaconRPCPlugin::createParent(QString path)
{
	QStringList tokens = path.split("/");
	auto        parent = m_app->dbRootObject();
	while (!tokens.isEmpty() && parent)
	{
		if (!tokens.first().trimmed().isEmpty())
		{
			ccHObject*           next{nullptr};
			ccHObject::Container filteredChildren;
			parent->filterChildren(filteredChildren, false, CC_TYPES::HIERARCHY_OBJECT, true);
			for (const auto& child : filteredChildren)
			{
				if (child->getName().compare(tokens.first().trimmed(), Qt::CaseInsensitive) == 0)
				{
					next = child;
					break;
				}
			}
			if (!next)
			{
				next = new ccHObject(tokens.first().trimmed());
				if (!next)
				{
					return parent;
				}
				parent->addChild(next);
				m_app->addToDB(next);
			}
			parent = next;
		}
		tokens.removeFirst();
	}
	return parent;
}
