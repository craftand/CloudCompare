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
#include <QTimer>
#include <QDir>
#include <cmath>

// local
#include "JsonRPCPlugin.h"

// CCCoreLib
#include <DistanceComputationTools.h>
#include <ScalarField.h>

// qCC_db
#include <ccColorScalesManager.h>
#include <ccGenericMesh.h>
#include <ccGenericPointCloud.h>
#include <ccHObjectCaster.h>
#include <ccOctree.h>
#include <ccPointCloud.h>
#include <ccScalarField.h>

// qCC_gl
#include <ccGLWindowInterface.h>
// qCC_io
#include <FileIOFilter.h>
// CC plugins
#include <ccMainAppInterface.h>

static ccGenericPointCloud* getAssociatedPointCloud(ccHObject* entity)
{
	if (!entity)
		return nullptr;

	if (entity->isA(CC_TYPES::POINT_CLOUD))
	{
		return static_cast<ccGenericPointCloud*>(entity);
	}

	if (entity->isKindOf(CC_TYPES::MESH))
	{
		return static_cast<ccGenericMesh*>(entity)->getAssociatedCloud();
	}

	ccHObject::Container clouds;
	entity->filterChildren(clouds, true, CC_TYPES::POINT_CLOUD);
	if (!clouds.empty())
	{
		return static_cast<ccGenericPointCloud*>(clouds.front());
	}

	ccHObject::Container meshes;
	entity->filterChildren(meshes, true, CC_TYPES::MESH);
	if (!meshes.empty())
	{
		return static_cast<ccGenericMesh*>(meshes.front())->getAssociatedCloud();
	}

	return nullptr;
}


BeaconRPCPlugin::BeaconRPCPlugin(QObject* parent)
	: QObject(parent)
	, ccStdPluginInterface(":/CC/plugin/BeaconRPCPlugin/info.json")
{
	qDebug() << "BeaconRPCPlugin::BeaconRPCPlugin";

	m_config = OrchestratorConfig::load();
	m_rpcClient.setConfig(m_config);
	m_sseClient.setConfig(m_config);

	connect(&m_sseClient, &SseClient::connected, this, [this]() {
		logInfo(QString("Connected to Desktop Orchestrator SSE stream at %1/stream")
				.arg(m_config.orchestratorUrl));
	});

	connect(&m_sseClient, &SseClient::disconnected, this, [this]() {
		logWarning("Disconnected from Desktop Orchestrator SSE stream. Reconnecting in background...");
	});

	connect(&m_sseClient, &SseClient::eventReceived,
			this,         &BeaconRPCPlugin::onSseEvent);

	connect(&m_sseClient, &SseClient::errorOccurred, this,
			[this](const QString& err) {
				logError(QString("SSE error: %1").arg(err));
			});

	registerMethods();
}

void BeaconRPCPlugin::registerMethods()
{
	m_methodHandlers["open"]             = [this](const auto& p) { return handleOpen(p); };
	m_methodHandlers["compare_scans"]    = [this](const auto& p) { return handleCompareScans(p); };
	m_methodHandlers["compute_distance"] = [this](const auto& p) { return handleComputeDistance(p); };
	m_methodHandlers["cc_to_fusion"]     = [this](const auto& p) { return handleCcToFusion(p); };
	m_methodHandlers["clear"]            = [this](const auto& p) { return handleClear(p); };
	m_methodHandlers["version"]          = [this](const auto& p) { return handleVersion(p); };
}

void BeaconRPCPlugin::logInfo(const QString& msg)
{
	QString formatted = QString("[BeaconRPC] %1").arg(msg);
	if (m_app)
	{
		m_app->dispToConsole(formatted, ccMainAppInterface::STD_CONSOLE_MESSAGE);
	}
	else
	{
		qDebug().noquote() << formatted;
	}
}

void BeaconRPCPlugin::logWarning(const QString& msg)
{
	QString formatted = QString("[BeaconRPC] WARNING: %1").arg(msg);
	if (m_app)
	{
		m_app->dispToConsole(formatted, ccMainAppInterface::WRN_CONSOLE_MESSAGE);
	}
	else
	{
		qWarning().noquote() << formatted;
	}
}

void BeaconRPCPlugin::logError(const QString& msg)
{
	QString formatted = QString("[BeaconRPC] ERROR: %1").arg(msg);
	if (m_app)
	{
		m_app->dispToConsole(formatted, ccMainAppInterface::ERR_CONSOLE_MESSAGE);
	}
	else
	{
		qCritical().noquote() << formatted;
	}
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

		// Auto-connect to orchestrator on startup without requiring manual button click
		QTimer::singleShot(200, this, [this]() {
			logInfo("Plugin loaded. Auto-connecting to Desktop Orchestrator...");
			bootstrapAndHandshake();
			m_sseClient.start();
		});
	}

	return {m_action};
}

void BeaconRPCPlugin::triggered(bool checked)
{
	logInfo(QString("Plugin manual toggle triggered: checked=%1").arg(checked));

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
			logInfo(QString("Connected to Desktop Orchestrator. Trace ID: %1").arg(m_traceId));
		}
		else
		{
			m_traceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
			m_rpcClient.setTraceId(m_traceId);
			logWarning(QString("Orchestrator unreachable at %1 (%2). Running offline. Trace: %3")
					   .arg(m_config.orchestratorUrl, reply->errorString(), m_traceId));
		}
		reply->deleteLater();
		bootNam->deleteLater();

		m_rpcClient.call("handshake", {}, [this](JsonRPCResult r) {
			if (!r.isError)
				logInfo(QString("Handshake successful: %1").arg(r.result.toString()));
			else
				logWarning(QString("Handshake failed: %1").arg(r.error_message));
		});
	});
}

void BeaconRPCPlugin::onSseEvent(QJsonObject payload)
{
	QString type   = payload["type"].toString();
	QString method = payload["method"].toString();
	auto    params = payload["params"].toVariant().toMap();

	logInfo(QString("Received SSE Event [type: %1, method: %2]").arg(type, method));

	if (method.isEmpty())
	{
		logWarning("Ignored SSE event: missing 'method' property");
		return;
	}

	JsonRPCResult result = executeCommand(method, params);

	if (result.isError)
	{
		logError(QString("Command '%1' failed [code %2]: %3")
				 .arg(method).arg(result.error_code).arg(result.error_message));

		QJsonObject logParams;
		logParams["level"]       = "ERROR";
		logParams["message"]     = QString("Command '%1' failed: %2").arg(method, result.error_message);
		logParams["filename"]    = "JsonRPCPlugin.cpp";
		logParams["line_number"] = 0;
		m_rpcClient.call("log_trace", logParams);
	}
	else
	{
		logInfo(QString("Command '%1' completed successfully").arg(method));

		if (method == "compare_scans" && result.result.isObject())
		{
			QJsonObject compResult = result.result.toObject();
			logInfo("Posting comparison metrics to Desktop Orchestrator...");
			m_rpcClient.call("report_comparison_result", compResult, [this](JsonRPCResult r) {
				if (!r.isError)
				{
					logInfo(QString("Inspection report generated successfully: %1")
							.arg(r.result.toObject()["html_report"].toString()));
				}
				else
				{
					logWarning(QString("Failed to generate inspection report: %1").arg(r.error_message));
				}
			});
		}
		else
		{
			QJsonObject logParams;
			logParams["level"]       = "INFO";
			logParams["message"]     = QString("Command '%1' completed successfully").arg(method);
			logParams["filename"]    = "JsonRPCPlugin.cpp";
			logParams["line_number"] = 0;
			m_rpcClient.call("log_trace", logParams);
		}
	}
}

JsonRPCResult BeaconRPCPlugin::executeCommand(const QString&               method,
											  const QMap<QString, QVariant>& params)
{
	qDebug() << "[BeaconRPC] executeCommand:" << method << params;

	if (m_app == nullptr)
	{
		return JsonRPCResult::error(-32603, "CloudCompare app interface not available");
	}

	auto it = m_methodHandlers.find(method);
	if (it == m_methodHandlers.end())
	{
		return JsonRPCResult::error(-32601, QString("Method not found: %1").arg(method));
	}

	JsonRPCResult result = it.value()(params);

	ccGLWindowInterface* win = m_app->getActiveGLWindow();
	if (win)
	{
		win->redraw();
	}

	return result;
}

JsonRPCResult BeaconRPCPlugin::handleOpen(const QMap<QString, QVariant>& params)
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

		logInfo(QString("Loaded model '%1' into CloudCompare DB").arg(filename));
		return JsonRPCResult::success(0);
	}
	else
	{
		return JsonRPCResult::error(1, "Failed to load model file (cancelled or format error)");
	}
}

JsonRPCResult BeaconRPCPlugin::handleCompareScans(const QMap<QString, QVariant>& params)
{
	QString baseline_path = params["baseline_obj_path"].toString();
	QString target_path   = params["target_obj_path"].toString();

	if (baseline_path.isEmpty() || target_path.isEmpty())
	{
		return JsonRPCResult::error(-32602, "Missing baseline_obj_path or target_obj_path in params");
	}

	logInfo(QString("Ingesting dual-scan comparison:\n - Baseline: %1\n - Target: %2")
			.arg(baseline_path, target_path));

	// 1. Open Baseline Mesh
	QMap<QString, QVariant> baseParams;
	baseParams["filename"] = baseline_path;
	baseParams["name"]     = "Baseline Scan";
	baseParams["silent"]   = true;
	JsonRPCResult r1 = handleOpen(baseParams);
	if (r1.isError)
	{
		return JsonRPCResult::error(r1.error_code, QString("Failed to load baseline scan: %1").arg(r1.error_message));
	}

	// 2. Open Target Mesh
	QMap<QString, QVariant> targetParams;
	targetParams["filename"] = target_path;
	targetParams["name"]     = "Target Scan";
	targetParams["silent"]   = true;
	JsonRPCResult r2 = handleOpen(targetParams);
	if (r2.isError)
	{
		return JsonRPCResult::error(r2.error_code, QString("Failed to load target scan: %1").arg(r2.error_message));
	}

	// 3. Compute Distance / Activate Heatmap
	QMap<QString, QVariant> distParams;
	distParams["baseline_name"]    = "Baseline Scan";
	distParams["target_name"]      = "Target Scan";
	distParams["baseline_scan_id"] = params.value("baseline_scan_id");
	distParams["target_scan_id"]   = params.value("target_scan_id");
	distParams["screenshot_path"]  = params.value("screenshot_path");
	distParams["model_3d_path"]    = params.value("model_3d_path");
	if (params.contains("tolerance_green"))
		distParams["tolerance_green"] = params["tolerance_green"];
	if (params.contains("tolerance_yellow"))
		distParams["tolerance_yellow"] = params["tolerance_yellow"];
	return handleComputeDistance(distParams);
}

JsonRPCResult BeaconRPCPlugin::handleComputeDistance(const QMap<QString, QVariant>& params)
{
	QString baseline_name = params["baseline_name"].toString();
	QString target_name   = params["target_name"].toString();

	if (baseline_name.isEmpty())
		baseline_name = "Baseline Scan";
	if (target_name.isEmpty())
		target_name = "Target Scan";

	ccHObject* root = m_app->dbRootObject();
	if (!root)
	{
		return JsonRPCResult::error(-32603, "DB root object not available");
	}

	ccHObject* baseline_obj = nullptr;
	ccHObject* target_obj   = nullptr;

	ccHObject::Container children;
	root->filterChildren(children, true, CC_TYPES::OBJECT);
	for (ccHObject* child : children)
	{
		if (child && child->getName().compare(baseline_name, Qt::CaseInsensitive) == 0)
			baseline_obj = child;
		if (child && child->getName().compare(target_name, Qt::CaseInsensitive) == 0)
			target_obj = child;
	}

	if (!baseline_obj || !target_obj)
	{
		return JsonRPCResult::error(-32602, QString("Baseline ('%1') or target ('%2') object not found in CloudCompare DB")
											   .arg(baseline_name, target_name));
	}

	ccGenericPointCloud* refGenCloud  = getAssociatedPointCloud(baseline_obj);
	ccGenericPointCloud* compGenCloud = getAssociatedPointCloud(target_obj);

	if (!refGenCloud || !compGenCloud || !compGenCloud->isA(CC_TYPES::POINT_CLOUD))
	{
		return JsonRPCResult::error(-32602, "Could not extract valid point clouds from baseline/target models for comparison");
	}

	ccPointCloud* compCloud = static_cast<ccPointCloud*>(compGenCloud);

	logInfo(QString("Starting C2C distance computation:\n - Reference (Baseline): %1 points\n - Compared (Target): %2 points")
			.arg(refGenCloud->size()).arg(compCloud->size()));

	// 1. Prepare / build octrees for spatial partitioning
	ccOctree::Shared compOctree = compCloud->getOctree();
	if (!compOctree)
	{
		compOctree = ccOctree::Shared(new ccOctree(compCloud));
		compOctree->build();
	}

	ccOctree::Shared refOctree;
	if (refGenCloud->isA(CC_TYPES::POINT_CLOUD))
	{
		ccPointCloud* refCloud = static_cast<ccPointCloud*>(refGenCloud);
		refOctree              = refCloud->getOctree();
		if (!refOctree)
		{
			refOctree = ccOctree::Shared(new ccOctree(refCloud));
			refOctree->build();
		}
	}

	// 2. Allocate & activate scalar field for distances
	const char* sfName = "C2C Absolute Distances";
	int sfIdx = compCloud->getScalarFieldIndexByName(sfName);
	if (sfIdx < 0)
	{
		sfIdx = compCloud->addScalarField(sfName);
	}
	if (sfIdx < 0)
	{
		return JsonRPCResult::error(-32603, "Could not allocate scalar field for distance computation");
	}

	compCloud->setCurrentScalarField(sfIdx);

	// 3. Setup computation parameters
	CCCoreLib::DistanceComputationTools::Cloud2CloudDistancesComputationParams distParams;
	distParams.octreeLevel    = 0; // Auto guess optimal level
	distParams.maxSearchDist  = 0; // No limit
	distParams.multiThread    = true;
	distParams.maxThreadCount = 0; // Use all CPU cores
	distParams.localModel     = CCCoreLib::NO_MODEL;

	// 4. Compute distances
	int result = CCCoreLib::DistanceComputationTools::computeCloud2CloudDistances(
		compCloud,
		refGenCloud,
		distParams,
		nullptr,
		compOctree.data(),
		refOctree.data());

	if (result < CCCoreLib::DistanceComputationTools::DISTANCE_COMPUTATION_RESULTS::SUCCESS)
	{
		compCloud->deleteScalarField(sfIdx);
		return JsonRPCResult::error(-32603, QString("Distance computation failed with code %1").arg(result));
	}

	// 5. Compute statistics & configure color scale
	CCCoreLib::ScalarField* sf = compCloud->getScalarField(sfIdx);
	ScalarType minVal = 0;
	ScalarType maxVal = 0;
	ScalarType meanVal = 0;
	ScalarType variance = 0;
	unsigned pointCount = compCloud->size();
	double sumSq = 0.0;

	// Tolerance bands (default 0.005 and 0.015 units / mm)
	double tolGreen = params.contains("tolerance_green") ? params["tolerance_green"].toDouble() : 0.005;
	double tolYellow = params.contains("tolerance_yellow") ? params["tolerance_yellow"].toDouble() : 0.015;
	unsigned countGreen = 0;
	unsigned countYellow = 0;
	unsigned countRed = 0;

	// 10-bin frequency distribution histogram
	const int numBins = 10;
	std::vector<unsigned> bins(numBins, 0);

	if (sf)
	{
		sf->computeMinAndMax();
		sf->computeMeanAndVariance(meanVal, &variance);
		minVal = sf->getMin();
		maxVal = sf->getMax();

		double binWidth = (maxVal > minVal) ? (static_cast<double>(maxVal - minVal) / numBins) : 1.0;

		for (unsigned i = 0; i < pointCount; ++i)
		{
			ScalarType val = sf->getValue(i);
			sumSq += static_cast<double>(val) * static_cast<double>(val);

			if (val <= tolGreen) countGreen++;
			else if (val <= tolYellow) countYellow++;
			else countRed++;

			if (binWidth > 0 && maxVal > minVal)
			{
				int binIdx = static_cast<int>((val - minVal) / binWidth);
				if (binIdx >= numBins) binIdx = numBins - 1;
				if (binIdx < 0) binIdx = 0;
				bins[binIdx]++;
			}
		}

		// Apply default BGYR (Blue-Green-Yellow-Red) heatmap color ramp
		ccScalarField* ccSf = static_cast<ccScalarField*>(sf);
		ccColorScale::Shared defaultScale = ccColorScalesManager::GetDefaultScale(ccColorScalesManager::BGYR);
		if (defaultScale)
		{
			ccSf->setColorScale(defaultScale);
		}
	}

	double rmsError = (pointCount > 0) ? std::sqrt(sumSq / pointCount) : 0.0;
	double stdDev = std::sqrt(variance);

	logInfo(QString("Distance computation complete: min = %1, max = %2, mean = %3, std_dev = %4, rms = %5")
			.arg(minVal).arg(maxVal).arg(meanVal).arg(stdDev).arg(rmsError));

	// 6. Activate scalar field display on point cloud
	compCloud->setCurrentDisplayedScalarField(sfIdx);
	compCloud->showSF(true);
	compCloud->showColors(false);
	compCloud->setVisible(true);
	compCloud->setEnabled(true);
	compCloud->colorsHaveChanged();
	compCloud->prepareDisplayForRefresh_recursive();

	// Enable SF display and disable materials/textures on mesh hierarchy so textures don't occlude heatmap
	ccHObject::Container targetMeshes;
	target_obj->filterChildren(targetMeshes, true, CC_TYPES::MESH);
	for (ccHObject* m : targetMeshes)
	{
		ccGenericMesh* mesh = static_cast<ccGenericMesh*>(m);
		mesh->showSF(true);
		mesh->showMaterials(false);
		mesh->showColors(false);
		mesh->setVisible(true);
		mesh->setEnabled(true);
		mesh->prepareDisplayForRefresh_recursive();
	}
	target_obj->showSF(true);
	target_obj->setVisible(true);
	target_obj->setEnabled(true);
	target_obj->prepareDisplayForRefresh_recursive();

	// Hide baseline object (and all its children) so it does not occlude the target comparison heatmap in the 3D viewport
	ccHObject::Container baseDescendants;
	baseline_obj->filterChildren(baseDescendants, true, CC_TYPES::OBJECT);
	for (ccHObject* c : baseDescendants)
	{
		if (c)
		{
			c->setVisible(false);
			c->prepareDisplayForRefresh_recursive();
		}
	}
	baseline_obj->setVisible(false);
	baseline_obj->prepareDisplayForRefresh_recursive();

	// 7. Refresh Viewport, Select Target in DB tree & Capture Screenshot
	QString screenshotPath = params["screenshot_path"].toString();
	if (screenshotPath.isEmpty())
	{
		screenshotPath = QDir::tempPath() + "/cloudcompare_c2c_heatmap.png";
	}

	if (m_app)
	{
		m_app->setSelectedInDB(baseline_obj, false);
		m_app->setSelectedInDB(target_obj, true);
		m_app->updatePropertiesView();
		m_app->redrawAll(false);
		ccGLWindowInterface* win = m_app->getActiveGLWindow();
		if (win)
		{
			win->zoomGlobal();
			win->redraw();

			QImage img = win->renderToImage(2, true);
			if (!img.isNull())
			{
				img.save(screenshotPath, "PNG");
				logInfo(QString("Saved comparison viewport snapshot to: %1").arg(screenshotPath));
			}
		}
	}

	// 8. Bake scalar field RGB colors and export 3D PLY model for WebGL report
	compCloud->reserveTheRGBTable();
	compCloud->resizeTheRGBTable();
	for (unsigned i = 0; i < pointCount; ++i)
	{
		const ccColor::Rgb* rgb = compCloud->getPointScalarValueColor(i);
		if (rgb)
		{
			compCloud->setPointColor(i, *rgb);
		}
	}
	compCloud->showColors(true);
	compCloud->colorsHaveChanged();

	QString model3dPath = params["model_3d_path"].toString();
	if (model3dPath.isEmpty())
	{
		model3dPath = QDir::tempPath() + "/cloudcompare_heatmap_model.ply";
	}

	FileIOFilter::SaveParameters saveParams;
	saveParams.alwaysDisplaySaveDialog = false;
	CC_FILE_ERROR saveResult = FileIOFilter::SaveToFile(target_obj, model3dPath, saveParams);
	if (saveResult == CC_FERR_NO_ERROR)
	{
		logInfo(QString("Exported 3D heatmap PLY model to: %1").arg(model3dPath));
	}
	else
	{
		logWarning(QString("Failed to export 3D heatmap PLY model (code %1)").arg(saveResult));
	}

	QJsonArray histogramArr;
	double histBinWidth = (maxVal > minVal) ? (static_cast<double>(maxVal - minVal) / numBins) : 1.0;
	for (int b = 0; b < numBins; ++b)
	{
		QJsonObject binObj;
		binObj["bin_start"] = static_cast<double>(minVal + b * histBinWidth);
		binObj["bin_end"]   = static_cast<double>(minVal + (b + 1) * histBinWidth);
		binObj["count"]     = static_cast<int>(bins[b]);
		histogramArr.append(binObj);
	}

	QJsonObject tolObj;
	tolObj["green_threshold"]   = tolGreen;
	tolObj["yellow_threshold"]  = tolYellow;
	tolObj["in_tolerance_count"] = static_cast<int>(countGreen);
	tolObj["in_tolerance_pct"]   = pointCount > 0 ? (100.0 * countGreen / pointCount) : 0.0;
	tolObj["warning_count"]      = static_cast<int>(countYellow);
	tolObj["warning_pct"]        = pointCount > 0 ? (100.0 * countYellow / pointCount) : 0.0;
	tolObj["out_of_spec_count"]  = static_cast<int>(countRed);
	tolObj["out_of_spec_pct"]    = pointCount > 0 ? (100.0 * countRed / pointCount) : 0.0;

	QJsonObject resData;
	resData["status"]           = "computed";
	resData["baseline"]         = baseline_name;
	resData["target"]           = target_name;
	resData["baseline_scan_id"] = params.value("baseline_scan_id").toString();
	resData["target_scan_id"]   = params.value("target_scan_id").toString();
	resData["scalar_field"]     = sfName;
	resData["point_count"]     = static_cast<int>(pointCount);
	resData["min_distance"]    = static_cast<double>(minVal);
	resData["max_distance"]    = static_cast<double>(maxVal);
	resData["mean_distance"]   = static_cast<double>(meanVal);
	resData["variance"]        = static_cast<double>(variance);
	resData["std_dev"]         = stdDev;
	resData["rms_error"]       = rmsError;
	resData["screenshot_path"] = screenshotPath;
	resData["model_3d_path"]   = model3dPath;
	resData["tolerance_bands"] = tolObj;
	resData["histogram"]       = histogramArr;

	return JsonRPCResult::success(resData);
}

JsonRPCResult BeaconRPCPlugin::handleCcToFusion(const QMap<QString, QVariant>& params)
{
	QString target_path = params["target_obj_path"].toString();
	if (target_path.isEmpty())
	{
		return JsonRPCResult::error(-32602, "Missing target_obj_path parameter");
	}

	QUrl            fusionUrl(m_config.orchestratorUrl + "/api/v1/fusion/import");
	QNetworkRequest req(fusionUrl);
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	if (!m_config.localToken.isEmpty())
	{
		req.setRawHeader("X-Local-Token", m_config.localToken.toUtf8());
	}

	QJsonObject body;
	body["type"]          = "send_to_fusion";
	body["obj_file_path"] = target_path;

	auto* nam   = new QNetworkAccessManager(this);
	auto* reply = nam->post(req, QJsonDocument(body).toJson());
	connect(reply, &QNetworkReply::finished, this, [this, reply, nam, target_path]() {
		if (reply->error() == QNetworkReply::NoError)
		{
			logInfo(QString("Successfully dispatched scan to Fusion: %1").arg(target_path));
		}
		else
		{
			logWarning(QString("Failed to dispatch scan to Fusion: %1").arg(reply->errorString()));
		}
		reply->deleteLater();
		nam->deleteLater();
	});

	QJsonObject resData;
	resData["status"]          = "dispatched_to_fusion";
	resData["target_obj_path"] = target_path;
	return JsonRPCResult::success(resData);
}

JsonRPCResult BeaconRPCPlugin::handleClear(const QMap<QString, QVariant>&)
{
	auto       root = m_app->dbRootObject();
	ccHObject* child;
	while ((child = root->getChild(0)) != nullptr)
	{
		m_app->removeFromDB(child, true);
	}
	logInfo("Cleared all objects from CloudCompare DB");
	return JsonRPCResult::success(0);
}

JsonRPCResult BeaconRPCPlugin::handleVersion(const QMap<QString, QVariant>&)
{
	return JsonRPCResult::success(m_version);
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
