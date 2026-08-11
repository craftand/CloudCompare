#pragma once

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QString>

#ifdef _WIN32
#  include <windows.h>
#  include <wincred.h>
#elif defined(__APPLE__)
#  include <CoreFoundation/CoreFoundation.h>
#  include <Security/Security.h>
#endif

//! Configuration loaded from config.json (written by beacon-desktop orchestrator).
struct OrchestratorConfig
{
	QString orchestratorUrl{"http://127.0.0.1:41979"};
	QString localToken{""};

	static OrchestratorConfig load()
	{
		OrchestratorConfig cfg;

		QStringList candidates;
		candidates << QCoreApplication::applicationDirPath() + "/beacon_config.json";
		candidates << QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
						  "/../craftand/beacon/config.json";
		candidates << QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
						  "/beacon/config.json";

		for (const QString& path : candidates)
		{
			QFile f(path);
			if (!f.open(QIODevice::ReadOnly))
				continue;

			QJsonParseError err;
			QJsonDocument   doc = QJsonDocument::fromJson(f.readAll(), &err);
			if (err.error != QJsonParseError::NoError || !doc.isObject())
				continue;

			QJsonObject obj = doc.object();
			if (obj.contains("orchestrator_url"))
				cfg.orchestratorUrl = obj["orchestrator_url"].toString();
			if (obj.contains("local_token"))
				cfg.localToken = obj["local_token"].toString();

			qDebug() << "[BeaconRPC] Config loaded from" << path;
			return cfg;
		}

		// Skip system credential store lookups in headless/CI test runners
		if (qEnvironmentVariableIsSet("QT_BEACON_SKIP_KEYSTORE") ||
			qEnvironmentVariableIsSet("CI") ||
			qEnvironmentVariableIsSet("CONTINUOUS_INTEGRATION"))
		{
			qDebug() << "[BeaconRPC] Skipping system credential store lookup (CI/Test mode)";
		}
		else
		{
			cfg.localToken = readTokenFromCredentialStore();
			if (!cfg.localToken.isEmpty())
				qDebug() << "[BeaconRPC] Token loaded from credential store";
			else
				qDebug() << "[BeaconRPC] No config found, using defaults (" << cfg.orchestratorUrl << ")";
		}

		return cfg;
	}

private:
	static QString readTokenFromCredentialStore()
	{
#ifdef _WIN32
		PCREDENTIALW pcred = nullptr;
		if (CredReadW(L"local_token@BeaconCADOrchestrator", CRED_TYPE_GENERIC, 0, &pcred))
		{
			QString token = QString::fromWCharArray(
				reinterpret_cast<const wchar_t*>(pcred->CredentialBlob),
				pcred->CredentialBlobSize / sizeof(wchar_t));
			CredFree(pcred);
			return token;
		}
#elif defined(__APPLE__)
		CFStringRef serviceRef = CFSTR("BeaconCADOrchestrator");
		CFStringRef accountRef = CFSTR("local_token");

		const void* keys[] = {
			kSecClass,
			kSecAttrService,
			kSecAttrAccount,
			kSecReturnData,
			kSecMatchLimit
		};

		const void* values[] = {
			kSecClassGenericPassword,
			serviceRef,
			accountRef,
			kCFBooleanTrue,
			kSecMatchLimitOne
		};

		CFDictionaryRef query = CFDictionaryCreate(
			kCFAllocatorDefault,
			keys,
			values,
			5,
			&kCFTypeDictionaryKeyCallBacks,
			&kCFTypeDictionaryValueCallBacks
		);

		CFDataRef resultData = nullptr;
		OSStatus status = SecItemCopyMatching(query, reinterpret_cast<CFTypeRef*>(&resultData));
		CFRelease(query);

		if (status == errSecSuccess && resultData != nullptr)
		{
			const char* bytes = reinterpret_cast<const char*>(CFDataGetBytePtr(resultData));
			CFIndex length = CFDataGetLength(resultData);
			QString token = QString::fromUtf8(bytes, static_cast<int>(length));
			CFRelease(resultData);
			return token;
		}
#endif
		return {};
	}
};
