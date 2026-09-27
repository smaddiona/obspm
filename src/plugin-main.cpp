/*
obspm - OBS Project Manager
Copyright (C) 2026 smaddiona

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/platform.h>
#include <plugin-support.h>

#include <cstring>

#include <QAbstractButton>
#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static const char *PROFILE_H = "obspm-h";
static const char *PROFILE_V = "obspm-v";

/* ---------- plugin config (<obs config>/plugin_config/obspm/config.json) ---------- */

struct Config {
	QString workdir;
	int hW = 1920, hH = 1080, vW = 1080, vH = 1920;
};
static Config cfg;
static bool sessionReady = false; // project chosen since OBS started
static QString currentProject;    // project of this session, empty until chosen
static QPushButton *dockButton = nullptr;

static const QList<QSize> H_PRESETS = {{1280, 720}, {1920, 1080}, {2560, 1440}, {3840, 2160}};
static const QList<QSize> V_PRESETS = {{720, 1280}, {1080, 1350}, {1080, 1920}, {1440, 2560}, {2160, 3840}};

static QString configPath()
{
	char *p = obs_module_config_path("config.json");
	QString s = QString::fromUtf8(p);
	bfree(p);
	return s;
}

static void loadConfig()
{
	obs_data_t *d = obs_data_create_from_json_file(configPath().toUtf8().constData());
	if (!d)
		return;
	cfg.workdir = QString::fromUtf8(obs_data_get_string(d, "workdir"));
	if (obs_data_has_user_value(d, "h_width")) {
		cfg.hW = (int)obs_data_get_int(d, "h_width");
		cfg.hH = (int)obs_data_get_int(d, "h_height");
		cfg.vW = (int)obs_data_get_int(d, "v_width");
		cfg.vH = (int)obs_data_get_int(d, "v_height");
	}
	obs_data_release(d);
}

static void saveConfig()
{
	QDir().mkpath(QFileInfo(configPath()).path());
	obs_data_t *d = obs_data_create();
	obs_data_set_string(d, "workdir", cfg.workdir.toUtf8().constData());
	obs_data_set_int(d, "h_width", cfg.hW);
	obs_data_set_int(d, "h_height", cfg.hH);
	obs_data_set_int(d, "v_width", cfg.vW);
	obs_data_set_int(d, "v_height", cfg.vH);
	obs_data_save_json_safe(d, configPath().toUtf8().constData(), "tmp", "bak");
	obs_data_release(d);
}

/* ---------- projects (<workdir>/obspm.json) ---------- */

static QString projectsFile()
{
	return QDir(cfg.workdir).filePath("obspm.json");
}

static QStringList loadProjects()
{
	QStringList out;
	obs_data_t *d = obs_data_create_from_json_file(projectsFile().toUtf8().constData());
	if (!d)
		return out;
	obs_data_array_t *arr = obs_data_get_array(d, "projects");
	for (size_t i = 0; i < obs_data_array_count(arr); i++) {
		obs_data_t *p = obs_data_array_item(arr, i);
		out << QString::fromUtf8(obs_data_get_string(p, "name"));
		obs_data_release(p);
	}
	obs_data_array_release(arr);
	obs_data_release(d);
	return out;
}

static void saveProjects(const QStringList &names)
{
	obs_data_t *d = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();
	for (const QString &n : names) {
		obs_data_t *p = obs_data_create();
		obs_data_set_string(p, "name", n.toUtf8().constData());
		obs_data_set_string(p, "path", QDir(cfg.workdir).filePath(n).toUtf8().constData());
		obs_data_array_push_back(arr, p);
		obs_data_release(p);
	}
	obs_data_set_array(d, "projects", arr);
	obs_data_save_json_safe(d, projectsFile().toUtf8().constData(), "tmp", "bak");
	obs_data_array_release(arr);
	obs_data_release(d);
}

/* ---------- OBS profiles / scene collections ---------- */

static bool listHas(char **list, const char *name)
{
	bool found = false;
	for (char **it = list; it && *it; it++)
		found |= strcmp(*it, name) == 0;
	bfree(list);
	return found;
}

// Creates obspm-h / obspm-v profiles and scene collections if missing, then restores the user's current ones.
static void ensureProfiles()
{
	char *origProfile = obs_frontend_get_current_profile();
	char *origScenes = obs_frontend_get_current_scene_collection();
	bool changed = false;

	for (const char *name : {PROFILE_H, PROFILE_V}) {
		if (!listHas(obs_frontend_get_profiles(), name)) {
			obs_frontend_set_current_profile(origProfile);
			obs_frontend_duplicate_profile(name); // keeps encoder/audio settings of the user's profile
			changed = true;
		}
		if (!listHas(obs_frontend_get_scene_collections(), name)) {
			obs_frontend_add_scene_collection(name);
			changed = true;
		}
	}
	if (changed) {
		obs_frontend_set_current_scene_collection(origScenes);
		obs_frontend_set_current_profile(origProfile);
	}
	bfree(origProfile);
	bfree(origScenes);
}

// Switches to the H/V profile + scene collection, applies resolution and recording path.
static void activate(bool vertical, const QString &recDir)
{
	const char *name = vertical ? PROFILE_V : PROFILE_H;
	obs_frontend_set_current_scene_collection(name);
	obs_frontend_set_current_profile(name);

	config_t *c = obs_frontend_get_profile_config();
	uint64_t w = vertical ? cfg.vW : cfg.hW, h = vertical ? cfg.vH : cfg.hH;
	config_set_uint(c, "Video", "BaseCX", w);
	config_set_uint(c, "Video", "BaseCY", h);
	config_set_uint(c, "Video", "OutputCX", w);
	config_set_uint(c, "Video", "OutputCY", h);
	QByteArray p = recDir.toUtf8();
	config_set_string(c, "SimpleOutput", "FilePath", p.constData());
	config_set_string(c, "AdvOut", "RecFilePath", p.constData());
	config_set_string(c, "AdvOut", "FFFilePath", p.constData());
	config_save_safe(c, "tmp", nullptr);
	obs_frontend_reset_video();
}

/* ---------- dialogs ---------- */

static QWidget *mainWindow()
{
	return static_cast<QWidget *>(obs_frontend_get_main_window());
}

// First-run wizard: explains the flow and asks for the main working folder.
static bool firstRunSetup()
{
	QDialog dlg(mainWindow());
	dlg.setWindowTitle("Welcome to obspm");
	dlg.setMinimumWidth(560);
	auto *layout = new QVBoxLayout(&dlg);

	auto *intro =
		new QLabel("<h3>obspm organizes your recordings into projects</h3>"
			   "<ol>"
			   "<li><b>Choose a main working folder</b> below. Every project becomes a subfolder here, "
			   "and the project list is saved in <i>obspm.json</i>.</li>"
			   "<li>The first time you press <b>Start Recording</b> after opening OBS, obspm asks which "
			   "project you are working on (pick one or type a new name) and whether you record "
			   "<b>horizontal</b> or <b>vertical</b>.</li>"
			   "<li>obspm then switches to the matching profile and scene collection "
			   "(<b>obspm-h</b> / <b>obspm-v</b>), applies the resolution and saves the recording in the "
			   "project folder. Further recordings in the same session go there directly.</li>"
			   "</ol>"
			   "<p><b>Tip:</b> set up your scenes once in <i>Scene Collection → obspm-h</i> and "
			   "<i>obspm-v</i>. You can change folder and resolutions anytime in "
			   "<i>Tools → obspm Settings</i>.</p>");
	intro->setWordWrap(true);
	layout->addWidget(intro);

	auto *dirEdit = new QLineEdit(
		cfg.workdir.isEmpty()
			? QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).filePath("obspm")
			: cfg.workdir);
	auto *browse = new QPushButton("Browse…");
	QObject::connect(browse, &QPushButton::clicked, [&] {
		QString d = QFileDialog::getExistingDirectory(&dlg, "obspm: main working folder", dirEdit->text());
		if (!d.isEmpty())
			dirEdit->setText(d);
	});
	auto *dirRow = new QHBoxLayout;
	dirRow->addWidget(new QLabel("Working folder:"));
	dirRow->addWidget(dirEdit);
	dirRow->addWidget(browse);
	layout->addLayout(dirRow);

	auto *bb = new QDialogButtonBox;
	auto *ok = bb->addButton("Continue", QDialogButtonBox::AcceptRole);
	bb->addButton("Later", QDialogButtonBox::RejectRole);
	QObject::connect(dirEdit, &QLineEdit::textChanged,
			 [=](const QString &t) { ok->setEnabled(!t.trimmed().isEmpty()); });
	QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	layout->addWidget(bb);

	if (dlg.exec() != QDialog::Accepted) {
		QMessageBox::information(mainWindow(), "obspm",
					 "No problem: obspm will ask again the first time you press Start Recording.");
		return false;
	}
	QString dir = dirEdit->text().trimmed();
	if (!QDir().mkpath(dir)) {
		QMessageBox::warning(mainWindow(), "obspm", "Cannot create folder:\n" + dir);
		return false;
	}
	cfg.workdir = dir;
	saveConfig();
	QMessageBox::information(mainWindow(), "obspm: ready",
				 "All set! Recordings will be organized in:\n" + dir +
					 "\n\nPress Start Recording to pick your first project.");
	return true;
}

static QString sizeText(const QSize &s)
{
	return QString("%1×%2").arg(s.width()).arg(s.height());
}

static void showSettings()
{
	QDialog dlg(mainWindow());
	dlg.setWindowTitle("obspm Settings");
	auto *form = new QFormLayout(&dlg);

	auto *dirEdit = new QLineEdit(cfg.workdir);
	auto *browse = new QPushButton("Browse…");
	QObject::connect(browse, &QPushButton::clicked, [&] {
		QString d = QFileDialog::getExistingDirectory(&dlg, "Working folder", dirEdit->text());
		if (!d.isEmpty())
			dirEdit->setText(d);
	});
	auto *dirRow = new QHBoxLayout;
	dirRow->addWidget(dirEdit);
	dirRow->addWidget(browse);
	form->addRow("Working folder", dirRow);

	auto spin = [](int v) {
		auto *s = new QSpinBox;
		s->setRange(16, 8192);
		s->setValue(v);
		return s;
	};
	auto *hW = spin(cfg.hW), *hH = spin(cfg.hH), *vW = spin(cfg.vW), *vH = spin(cfg.vH);
	// preset combo fills the spinboxes; spinboxes stay editable for custom sizes
	auto pair = [](QSpinBox *a, QSpinBox *b, const QList<QSize> &presets) {
		auto *combo = new QComboBox;
		combo->addItem("Custom");
		for (const QSize &s : presets)
			combo->addItem(sizeText(s), s);
		auto sync = [=] {
			int i = combo->findData(QSize(a->value(), b->value()));
			combo->setCurrentIndex(i < 0 ? 0 : i);
		};
		sync();
		QObject::connect(combo, &QComboBox::activated, [=](int i) {
			if (i == 0)
				return;
			QSize s = combo->itemData(i).toSize();
			a->setValue(s.width());
			b->setValue(s.height());
		});
		QObject::connect(a, &QSpinBox::valueChanged, sync);
		QObject::connect(b, &QSpinBox::valueChanged, sync);
		auto *l = new QHBoxLayout;
		l->addWidget(combo);
		l->addWidget(a);
		l->addWidget(new QLabel("×"));
		l->addWidget(b);
		return l;
	};
	form->addRow("Horizontal (obspm-h)", pair(hW, hH, H_PRESETS));
	form->addRow("Vertical (obspm-v)", pair(vW, vH, V_PRESETS));

	auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	form->addRow(bb);

	if (dlg.exec() != QDialog::Accepted)
		return;
	cfg.workdir = dirEdit->text();
	cfg.hW = hW->value();
	cfg.hH = hH->value();
	cfg.vW = vW->value();
	cfg.vH = vH->value();
	saveConfig();
	// ponytail: new resolution applies on next project pick; restart OBS or re-pick to apply mid-session
}

static QString nameError(const QString &name, const QStringList &projects)
{
	if (name.isEmpty())
		return "Type a name for the project.";
	if (name.contains('/') || name.contains('\\') || name.contains(':') || name.startsWith('.'))
		return "The name can't contain / \\ : or start with a dot.";
	if (projects.contains(name, Qt::CaseInsensitive))
		return "A project with this name already exists.";
	return {};
}

static void updateDockButton()
{
	if (!dockButton)
		return;
	if (currentProject.isEmpty()) {
		dockButton->setText("obspm · Choose project…");
		return;
	}
	char *cur = obs_frontend_get_current_profile();
	bool v = strcmp(cur, PROFILE_V) == 0;
	bfree(cur);
	dockButton->setText(QString("obspm · %1 · %2 %3")
				    .arg(currentProject, v ? "Vertical" : "Horizontal",
					 sizeText(v ? QSize(cfg.vW, cfg.vH) : QSize(cfg.hW, cfg.hH))));
}

// Session dialog: manage projects, pick project + orientation + resolution.
// startRecording only changes the confirm button label. Returns true if the user confirmed.
static bool chooseProject(bool startRecording)
{
	if (cfg.workdir.isEmpty() && !firstRunSetup())
		return false;

	QStringList projects = loadProjects();
	QDir root(cfg.workdir);

	QDialog dlg(mainWindow());
	dlg.setWindowTitle("obspm: projects & orientation");
	dlg.setMinimumWidth(500);
	auto *layout = new QVBoxLayout(&dlg);

	auto *step1 = new QLabel("<h3>1 · Choose a project</h3>"
				 "Continue an existing project or create a new one. "
				 "All recordings of this session will be saved in its folder.");
	step1->setWordWrap(true);
	layout->addWidget(step1);

	auto *list = new QListWidget;
	layout->addWidget(list);

	auto *nameEdit = new QLineEdit;
	nameEdit->setPlaceholderText("Name of the new project, e.g. Tutorial 12");
	layout->addWidget(nameEdit);

	auto *renameBtn = new QPushButton("Rename…");
	auto *deleteBtn = new QPushButton("Delete…");
	auto *manageRow = new QHBoxLayout;
	manageRow->addStretch();
	manageRow->addWidget(renameBtn);
	manageRow->addWidget(deleteBtn);
	layout->addLayout(manageRow);

	layout->addWidget(new QLabel("<h3>2 · Choose the orientation</h3>"));
	auto orientButton = [](const QString &text) {
		auto *b = new QToolButton;
		b->setText(text);
		b->setCheckable(true);
		b->setMinimumHeight(56);
		b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		return b;
	};
	auto *horiz = orientButton("▭  Horizontal");
	auto *vert = orientButton("▯  Vertical");
	auto *group = new QButtonGroup(&dlg); // exclusive by default
	group->addButton(horiz);
	group->addButton(vert);
	char *cur = obs_frontend_get_current_profile();
	(strcmp(cur, PROFILE_V) == 0 ? vert : horiz)->setChecked(true);
	bfree(cur);
	auto *orientRow = new QHBoxLayout;
	orientRow->addWidget(horiz);
	orientRow->addWidget(vert);
	layout->addLayout(orientRow);

	layout->addWidget(new QLabel("<h3>3 · Choose the resolution</h3>"));
	auto *resCombo = new QComboBox;
	layout->addWidget(resCombo);

	auto *summary = new QLabel;
	summary->setWordWrap(true);
	layout->addWidget(summary);

	auto *bb = new QDialogButtonBox;
	auto *confirm = bb->addButton(startRecording ? "● Start recording" : "✓ Apply", QDialogButtonBox::AcceptRole);
	confirm->setDefault(true);
	bb->addButton(QDialogButtonBox::Cancel);
	QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
	QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
	layout->addWidget(bb);

	auto fillResolutions = [&] {
		bool v = vert->isChecked();
		QSize current = v ? QSize(cfg.vW, cfg.vH) : QSize(cfg.hW, cfg.hH);
		resCombo->clear();
		for (const QSize &s : v ? V_PRESETS : H_PRESETS)
			resCombo->addItem(sizeText(s), s);
		if (resCombo->findData(current) < 0) // custom size from Settings
			resCombo->addItem(sizeText(current) + " (custom)", current);
		resCombo->setCurrentIndex(resCombo->findData(current));
	};

	// row 0 = "New project…", then projects most recent first
	auto fillList = [&](const QString &select) {
		list->clear();
		auto *newItem = new QListWidgetItem("+  New project…");
		QFont bold = newItem->font();
		bold.setBold(true);
		newItem->setFont(bold);
		list->addItem(newItem);
		for (auto it = projects.crbegin(); it != projects.crend(); ++it)
			list->addItem(*it);
		auto found = list->findItems(select, Qt::MatchExactly);
		list->setCurrentRow(!found.isEmpty() ? list->row(found.first()) : (projects.isEmpty() ? 0 : 1));
	};

	QString name;
	auto update = [&] {
		bool isNew = list->currentRow() <= 0;
		nameEdit->setVisible(isNew);
		renameBtn->setEnabled(!isNew);
		deleteBtn->setEnabled(!isNew);
		name = isNew ? nameEdit->text().trimmed() : list->currentItem()->text();
		QString err = isNew ? nameError(name, projects) : QString();
		summary->setText(err.isEmpty() ? "Recordings will be saved in:<br><b>" +
							 root.filePath(name).toHtmlEscaped() + "</b>"
					       : "<span style='color:#e5534b'>" + err + "</span>");
		confirm->setEnabled(err.isEmpty());
	};

	QObject::connect(list, &QListWidget::currentRowChanged, [&](int row) {
		update();
		if (row == 0)
			nameEdit->setFocus();
	});
	QObject::connect(nameEdit, &QLineEdit::textChanged, update);
	QObject::connect(list, &QListWidget::itemDoubleClicked, [&] {
		if (confirm->isEnabled())
			dlg.accept();
	});
	QObject::connect(group, &QButtonGroup::buttonClicked, fillResolutions);

	QObject::connect(renameBtn, &QPushButton::clicked, [&] {
		QString oldName = list->currentItem()->text();
		bool ok = false;
		QString newName = QInputDialog::getText(&dlg, "Rename project",
							"New name for \"" + oldName + "\":", QLineEdit::Normal, oldName,
							&ok)
					  .trimmed();
		if (!ok || newName == oldName)
			return;
		QStringList others = projects;
		others.removeAll(oldName); // allow case-only renames
		QString err = nameError(newName, others);
		if (!err.isEmpty()) {
			QMessageBox::warning(&dlg, "obspm", err);
			return;
		}
		if (root.exists(oldName) && !root.rename(oldName, newName)) {
			QMessageBox::warning(&dlg, "obspm", "Cannot rename the folder. Is a file in it open?");
			return;
		}
		projects[projects.indexOf(oldName)] = newName;
		saveProjects(projects);
		if (currentProject == oldName) { // keep this session recording into the renamed folder
			currentProject = newName;
			config_t *c = obs_frontend_get_profile_config();
			QByteArray p = root.filePath(newName).toUtf8();
			config_set_string(c, "SimpleOutput", "FilePath", p.constData());
			config_set_string(c, "AdvOut", "RecFilePath", p.constData());
			config_set_string(c, "AdvOut", "FFFilePath", p.constData());
			config_save_safe(c, "tmp", nullptr);
			updateDockButton();
		}
		fillList(newName);
	});

	QObject::connect(deleteBtn, &QPushButton::clicked, [&] {
		QString target = list->currentItem()->text();
		QMessageBox box(QMessageBox::Question, "Delete project",
				"Delete project \"" + target +
					"\"?\n\nYou can remove it from the list only, "
					"or also move its folder (with all recordings) to the Trash.",
				QMessageBox::NoButton, &dlg);
		auto *listOnly = box.addButton("Remove from list", QMessageBox::AcceptRole);
		auto *trash = box.addButton("Move folder to Trash", QMessageBox::DestructiveRole);
		box.addButton(QMessageBox::Cancel);
		box.exec();
		if (box.clickedButton() != listOnly && box.clickedButton() != trash)
			return;
		if (box.clickedButton() == trash && root.exists(target) && !QFile::moveToTrash(root.filePath(target))) {
			QMessageBox::warning(&dlg, "obspm", "Cannot move the folder to the Trash.");
			return;
		}
		projects.removeAll(target);
		saveProjects(projects);
		if (currentProject == target) { // ask again at next recording
			currentProject.clear();
			sessionReady = false;
			updateDockButton();
		}
		fillList({});
	});

	fillResolutions();
	fillList(currentProject); // preselect this session's project, else the last used one
	update();

	if (dlg.exec() != QDialog::Accepted)
		return false;

	QString dir = root.filePath(name);
	if (!QDir().mkpath(dir)) {
		QMessageBox::warning(mainWindow(), "obspm", "Cannot create folder:\n" + dir);
		return false;
	}
	if (!projects.contains(name)) {
		projects << name;
		saveProjects(projects);
	}
	QSize res = resCombo->currentData().toSize();
	if (vert->isChecked()) {
		cfg.vW = res.width();
		cfg.vH = res.height();
	} else {
		cfg.hW = res.width();
		cfg.hH = res.height();
	}
	saveConfig();
	activate(vert->isChecked(), dir);
	currentProject = name;
	sessionReady = true;
	updateDockButton();
	return true;
}

/* ---------- intercept the "Start Recording" button ---------- */

class RecordFilter : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *obj, QEvent *ev) override
	{
		if (sessionReady || obs_frontend_recording_active())
			return false;
		if (ev->type() == QEvent::MouseButtonPress)
			return true; // swallow, act on release
		if (ev->type() != QEvent::MouseButtonRelease)
			return false;
		auto *btn = static_cast<QAbstractButton *>(obj);
		btn->setDown(false);
		if (!btn->rect().contains(static_cast<QMouseEvent *>(ev)->position().toPoint()))
			return true; // released outside the button = no click
		if (chooseProject(true))
			// let OBS finish rebuilding outputs after the profile switch
			QTimer::singleShot(0, [] { obs_frontend_recording_start(); });
		return true;
	}
};

// Adds the "obspm · …" button to the Controls dock, right under the recording row.
static void addDockButton(QAbstractButton *recordButton)
{
	auto *frame = mainWindow()->findChild<QWidget *>("controlsFrame");
	auto *vbox = frame ? qobject_cast<QVBoxLayout *>(frame->layout()) : nullptr;
	if (!vbox) {
		obs_log(LOG_WARNING, "controls dock layout not found, dock button disabled");
		return;
	}
	int index = vbox->count();
	for (int i = 0; i < vbox->count(); i++)
		if (vbox->itemAt(i)->layout() && vbox->itemAt(i)->layout()->indexOf(recordButton) >= 0)
			index = i + 1;

	dockButton = new QPushButton;
	dockButton->setSizePolicy(QSizePolicy::Ignored,
				  QSizePolicy::Fixed); // long project names must not widen the dock
	dockButton->setToolTip("Change project, orientation and resolution");
	QObject::connect(dockButton, &QPushButton::clicked, [] { chooseProject(false); });
	vbox->insertWidget(index, dockButton);
	updateDockButton();
}

static void onEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
		loadConfig();
		ensureProfiles(); // before the wizard, so obspm-h / obspm-v already exist when it mentions them
		if (cfg.workdir.isEmpty())
			firstRunSetup();

		auto *btn = mainWindow()->findChild<QAbstractButton *>("recordButton");
		if (!btn) {
			obs_log(LOG_WARNING, "recordButton not found, project picker disabled");
			return;
		}
		btn->installEventFilter(new RecordFilter(btn));
		addDockButton(btn);
		break;
	}
	// profile/scene switches are impossible while recording
	case OBS_FRONTEND_EVENT_RECORDING_STARTING:
		if (dockButton)
			dockButton->setEnabled(false);
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
		if (dockButton)
			dockButton->setEnabled(true);
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	auto *action = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("obspm Settings"));
	QObject::connect(action, &QAction::triggered, showSettings);
	obs_frontend_add_event_callback(onEvent, nullptr);
	obs_log(LOG_INFO, "plugin loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(onEvent, nullptr);
}
