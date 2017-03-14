#include "JavaProject.h"

#include "component/view/DialogView.h"
#include "data/indexer/IndexerCommandJava.h"
#include "data/parser/java/JavaEnvironmentFactory.h"
#include "data/parser/java/JavaEnvironment.h"
#include "utility/file/FileRegister.h"
#include "utility/file/FileSystem.h"
#include "utility/messaging/type/MessageStatus.h"
#include "utility/text/TextAccess.h"
#include "utility/ResourcePaths.h"
#include "utility/utilityMaven.h"
#include "utility/utilityString.h"
#include "Application.h"
#include "settings/ApplicationSettings.h"

#include "utility/tracing.h"
#include "utility/ScopedFunctor.h"

JavaProject::JavaProject(
	std::shared_ptr<JavaProjectSettings> projectSettings, StorageAccessProxy* storageAccessProxy, std::shared_ptr<DialogView> dialogView
)
	: Project(storageAccessProxy, dialogView)
	, m_projectSettings(projectSettings)
{
}

JavaProject::~JavaProject()
{
}

std::shared_ptr<ProjectSettings> JavaProject::getProjectSettings()
{
	return m_projectSettings;
}

const std::shared_ptr<ProjectSettings> JavaProject::getProjectSettings() const
{
	return m_projectSettings;
}

bool JavaProject::prepareIndexing()
{
	m_rootDirectories.reset();

	std::string errorString;
	if (!JavaEnvironmentFactory::getInstance())
	{
#ifdef _WIN32
		const std::string separator = ";";
#else
		const std::string separator = ":";
#endif
		JavaEnvironmentFactory::createInstance(
			ResourcePaths::getJavaPath() + "guava-18.0.jar" + separator +
			ResourcePaths::getJavaPath() + "java-indexer.jar" + separator +
			ResourcePaths::getJavaPath() + "javaparser-core.jar" + separator +
			ResourcePaths::getJavaPath() + "javaslang-2.0.3.jar" + separator +
			ResourcePaths::getJavaPath() + "javassist-3.19.0-GA.jar" + separator +
			ResourcePaths::getJavaPath() + "java-symbol-solver-core.jar" + separator +
			ResourcePaths::getJavaPath() + "java-symbol-solver-logic.jar" + separator +
			ResourcePaths::getJavaPath() + "java-symbol-solver-model.jar",
			errorString
		);
	}

	if (errorString.size() > 0)
	{
		LOG_ERROR(errorString);
		MessageStatus(errorString, true, false).dispatch();
	}

	if (!JavaEnvironmentFactory::getInstance())
	{
		std::string dialogMessage =
			"Coati was unable to locate Java on this machine.\n"
			"Please make sure to provide the correct Java Path in the preferences.";

		if (errorString.size() > 0)
		{
			dialogMessage += "\n\nError: " + errorString;
		}

		MessageStatus(dialogMessage, true, false).dispatch();

		Application::getInstance()->handleDialog(dialogMessage);
		return false;
	}

	if (m_projectSettings->getAbsoluteMavenProjectFilePath().exists())
	{
		const FilePath mavenPath = ApplicationSettings::getInstance()->getMavenPath();
		const FilePath projectRootPath = m_projectSettings->getAbsoluteMavenProjectFilePath().parentDirectory();

		if (Application::getInstance()->hasGUI())
		{
			ScopedFunctor dialogHider([this](){
				getDialogView()->hideStatusDialog();
			});

			getDialogView()->showStatusDialog("Preparing Project", "Maven\nGenerating Source Files");
			bool success = utility::mavenGenerateSources(
				mavenPath, projectRootPath
			);

			if (!success)
			{
				const std::string dialogMessage =
					"Coati was unable to locate Maven on this machine.\n"
					"Please make sure to provide the correct Maven Path in the preferences.";

				MessageStatus(dialogMessage, true, false).dispatch();

				Application::getInstance()->handleDialog(dialogMessage);
				return false;
			}

			getDialogView()->showStatusDialog("Preparing Project", "Maven\nExporting Dependencies");
		}
		utility::mavenCopyDependencies(
			mavenPath, projectRootPath, m_projectSettings->getAbsoluteMavenDependenciesDirectory()
		);
	}

	return true;
}

std::vector<std::shared_ptr<IndexerCommand>> JavaProject::getIndexerCommands()
{
	std::vector<FilePath> classPath;

	for (const FilePath& p: m_projectSettings->getAbsoluteClasspaths())
	{
		if (p.exists())
		{
			classPath.push_back(p);
		}
	}

	if (m_projectSettings->getAbsoluteMavenDependenciesDirectory().exists())
	{
		const std::vector<std::string> dependencies = FileSystem::getFileNamesFromDirectory(
			m_projectSettings->getAbsoluteMavenDependenciesDirectory().str(),
			std::vector<std::string>(1, ".jar")
		);
		for (const std::string& dependency: dependencies)
		{
			classPath.push_back(dependency);
		}
	}

	if (!m_rootDirectories)
	{
		if (Application::getInstance()->hasGUI())
		{
			getDialogView()->showStatusDialog("Preparing Project", "Gathering Root\nDirectories");
		}
		fetchRootDirectories();
		if (Application::getInstance()->hasGUI())
		{
			getDialogView()->hideStatusDialog();
		}
	}

	for (FilePath rootDirectory: *(m_rootDirectories.get()))
	{
		if (rootDirectory.exists())
		{
			classPath.push_back(rootDirectory.str());
		}
	}

	std::set<FilePath> indexedPaths;
	for (FilePath p: m_projectSettings->getAbsoluteSourcePaths())
	{
		if (p.exists())
		{
			indexedPaths.insert(p);
		}
	}

	std::set<FilePath> excludedPaths;
	for (FilePath p: m_projectSettings->getAbsoluteExcludePaths())
	{
		if (p.exists())
		{
			excludedPaths.insert(p);
		}
	}

	std::vector<std::shared_ptr<IndexerCommand>> indexerCommands;
	for (const FilePath& sourcePath: getSourceFilePaths())
	{
		indexerCommands.push_back(std::make_shared<IndexerCommandJava>(sourcePath, indexedPaths, excludedPaths, classPath));
	}

	return indexerCommands;
}

void JavaProject::updateFileManager(FileManager& fileManager)
{
	std::vector<FilePath> sourcePaths;
	if (m_projectSettings->getAbsoluteMavenProjectFilePath().exists())
	{

		if (Application::getInstance()->hasGUI())
		{
			getDialogView()->showStatusDialog("Preparing Project", "Maven\nFetching Source Directories");
		}

		const FilePath mavenPath(ApplicationSettings::getInstance()->getMavenPath());
		const FilePath projectRootPath = m_projectSettings->getAbsoluteMavenProjectFilePath().parentDirectory();
		sourcePaths = utility::mavenGetAllDirectoriesFromEffectivePom(mavenPath, projectRootPath, m_projectSettings->getShouldIndexMavenTests());


		if (Application::getInstance()->hasGUI())
		{
			getDialogView()->hideStatusDialog();
		}
	}
	else
	{
		sourcePaths = m_projectSettings->getAbsoluteSourcePaths();
	}
	std::vector<FilePath> headerPaths = sourcePaths;
	std::vector<std::string> sourceExtensions = m_projectSettings->getSourceExtensions();
	std::vector<FilePath> excludePaths = m_projectSettings->getAbsoluteExcludePaths();

	fileManager.setPaths(sourcePaths, headerPaths, excludePaths, sourceExtensions);
}

void JavaProject::fetchRootDirectories()
{
	m_rootDirectories = std::make_shared<std::set<FilePath>>();

	FileManager fileManager;
	updateFileManager(fileManager);

	FileManager::FileSets fileSets = fileManager.fetchFilePaths(std::vector<FileInfo>());
	std::shared_ptr<JavaEnvironment> javaEnvironment = JavaEnvironmentFactory::getInstance()->createEnvironment();

	for (FilePath filePath: fileSets.addedFiles)
	{
		std::shared_ptr<TextAccess> textAccess = TextAccess::createFromFile(filePath.str());

		std::string packageName = "";
		javaEnvironment->callStaticMethod("io/coati/JavaIndexer", "getPackageName", packageName, textAccess->getText());

		if (packageName.empty())
		{
			continue;
		}

		FilePath rootPath = filePath.parentDirectory();
		bool success = true;

		const std::vector<std::string> packageNameParts = utility::splitToVector(packageName, ".");
		for (std::vector<std::string>::const_reverse_iterator it = packageNameParts.rbegin(); it != packageNameParts.rend(); it++)
		{
			if (rootPath.fileName() != (*it))
			{
				success = false;
				break;
			}
			rootPath = rootPath.parentDirectory();
		}

		if (success)
		{
			m_rootDirectories->insert(rootPath);
		}
	}
}
