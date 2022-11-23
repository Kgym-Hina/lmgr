#include "flash.h"

flashDialog::flashDialog(QWidget *parent, bool mode) : QDialog(parent)
{
	setupUi(this);

	setWindowFlags(Qt::Window | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);

	buttonBox->button(QDialogButtonBox::Retry)->setEnabled(false);

	if(mode == DUMP)
	{
		setWindowTitle(tr("提取 LOGO 镜像"));

		label_4->setDisabled(true);
		label_fbt->setDisabled(true);
	}

	usbmode = mode;

	QTimer::singleShot(250, this, SLOT(initADB()));
}

void flashDialog::prg_errorOccurred(__attribute__((unused)) QProcess::ProcessError error)
{
	QMessageBox::critical(this, APPNAME,tr("润不了指令：\n\n%1").arg(process->errorString()));

	failed = true;
	running = false;
}

void flashDialog::prg_readyReadStandardOutput()
{
	QString response = process->readAll();

	response.replace("\r\n", "\n");

	if(response != "\r\n" && response != "\n")
	{
		process_output = response;

		if(!response.endsWith("\n"))
		{
			response.append("\n");
		}

		if(response.contains("error") || response.contains("FAILED") || response.contains("unauthorized") || response.contains("permissions") || response.contains("denied"))
		{
			textcolor = textEdit->textColor();

			textEdit->setTextColor(Qt::red);

			textEdit->append(response.replace("\n\n", "\n"));

			textEdit->setTextColor(textcolor);
		}
		else
		{
			textEdit->append(response.replace("\n\n", "\n"));
		}
	}
}

void flashDialog::prg_finished(__attribute__((unused)) int exitCode, __attribute__((unused)) QProcess::ExitStatus exitStatus)
{
	running = false;
}

void flashDialog::sendCommand(QString cmd)
{
	textcolor = textEdit->textColor();
	fontweight = textEdit->fontWeight();

	textEdit->setTextColor(Qt::blue);
	textEdit->setFontWeight(QFont::Bold);

	textEdit->append(cmd);

	textEdit->setTextColor(textcolor);
	textEdit->setFontWeight(fontweight);

	failed = false;
	running = true;

	process->start(QString("%1/%2").arg(QApplication::applicationDirPath()).arg(cmd));

	while(running)
	{
		QApplication::processEvents();
	}
}

void flashDialog::initADB()
{
	process = new QProcess(this);

	process->setProcessChannelMode(QProcess::MergedChannels);

	connect(process, SIGNAL(errorOccurred(QProcess::ProcessError)), this, SLOT(prg_errorOccurred(QProcess::ProcessError)));
	connect(process, SIGNAL(readyReadStandardOutput()), this, SLOT(prg_readyReadStandardOutput()));
	connect(process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(prg_finished(int, QProcess::ExitStatus)));

	sendCommand("adb devices");

	if(process_output.contains("List of devices attached\n\n"))
	{
		label_adb->setPixmap(QPixmap(":/png/png/con-err.png"));

		buttonBox->button(QDialogButtonBox::Retry)->setEnabled(true);

		QMessageBox::warning(this, APPNAME, tr("无设备\n\n用数据线连接后再试"));
	}
	else if(process_output.contains("unauthorized"))
	{
		label_serial->setText(process_output.split("\n").at(1).split("\t").at(0));

		label_adb->setPixmap(QPixmap(":/png/png/con-err.png"));

		buttonBox->button(QDialogButtonBox::Retry)->setEnabled(true);

		QMessageBox::warning(this, APPNAME, tr("未授权\n\n请在手机上点击允许调试"));
	}
	else if(process_output.contains("permissions"))
	{
		label_serial->setText(process_output.split("\n").at(1).split("\t").at(0));

		label_adb->setPixmap(QPixmap(":/png/png/con-err.png"));

		buttonBox->button(QDialogButtonBox::Retry)->setEnabled(true);

		QMessageBox::warning(this, APPNAME, tr("无权限n\n请修复后再试"));
	}
	else if(process_output.contains("List of devices attached\n"))
	{
		buttonBox->button(QDialogButtonBox::Retry)->setEnabled(false);

		label_serial->setText(process_output.split("\n").at(1).split("\t").at(0));

		label_adb->setPixmap(QPixmap(":/png/png/con-suc.png"));

		sendCommand("adb shell getprop ro.product.model");

		label_model->setText(process_output.simplified());

		sendCommand("adb shell getprop ro.product.device");

		label_device->setText(process_output.simplified());

		foreach(QString device, devices)
		{
			supported = false;

			if(device == label_device->text())
			{
				supported = true;

				break;
			}
		}

		if(!supported)
		{
			QMessageBox::warning(this, APPNAME, tr("你的设备未被测试过!\n\n请自行承担使用风险"));
		}

		if(usbmode == FLASH)
		{
			startFlashing();
		}
		else
		{
			startDumping();
		}
	}
	else if(!failed)
	{
		QMessageBox::warning(this, APPNAME, tr("超出预期的回复"));
	}
}

void flashDialog::startFlashing()
{
	if(QMessageBox::question(this, APPNAME, tr("现在开刷？")) == QMessageBox::Yes)
	{
		sendCommand("adb reboot-bootloader");

		textEdit->append("\n等待设备重启中...\n");

		QCoreApplication::processEvents();

		QThread::msleep(5000);

		label_fbt->setPixmap(QPixmap(":/png/png/con-err.png"));

		abort = false;

		do
		{
			QThread::msleep(1000);

			sendCommand("fastboot devices");
		}
		while(!abort && (process_output.isEmpty() || !process_output.contains("\tfastboot\n")));

		if(abort)
		{
			QMessageBox::warning(this, APPNAME, tr("已取消"));
		}
		else
		{
			label_fbt->setPixmap(QPixmap(":/png/png/con-suc.png"));

			sendCommand("fastboot flash logo " + reinterpret_cast<MainWindow*>(parent())->flash_file);

			if(process_output.contains("error") || process_output.contains("FAILED"))
			{
				QMessageBox::warning(this, APPNAME, tr("刷坏了（悲）\n\n详见日志"));
			}
			else
			{
				QMessageBox::information(this, APPNAME, tr("刷好了（喜）\n\n点击重启设备"));
			}

			sendCommand("fastboot reboot");
		}
	}
}

void flashDialog::startDumping()
{
	sendCommand("adb shell \"su -c dd if=/dev/block/bootdevice/by-name/logo of=/sdcard/logo.img\"");

	if(process_output.contains("denied"))
	{
		buttonBox->button(QDialogButtonBox::Retry)->setEnabled(true);

		QMessageBox::warning(this, APPNAME, tr("无权限\n\n在？给个 root"));
	}
	else if(process_output.contains("copied"))
	{
		sendCommand("adb pull /sdcard/logo.img " + QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/logo.img");

		if(process_output.contains("pulled"))
		{
			QMessageBox::information(this, APPNAME, tr("提取完成"));
		}
		else
		{
			QMessageBox::warning(this, APPNAME, tr("提取失败\n\n详见日志"));
		}
	}
	else if(!failed)
	{
		QMessageBox::warning(this, APPNAME, tr("超出预期的回复"));
	}
}

void flashDialog::accept()
{
	if(usbmode == FLASH)
	{
		initADB();
	}
	else
	{
		label_model->text() == "?" ? initADB() : startDumping();
	}
}

void flashDialog::reject()
{
	if(running)
	{
		if(QMessageBox::question(this, APPNAME, tr("确定要取消?")) == QMessageBox::Yes)
		{
			abort = true;
			running = false;
		}
	}
	else
	{
		if(!failed)
		{
			sendCommand("adb kill-server");
		}

		QDialog::reject();
	}
}
