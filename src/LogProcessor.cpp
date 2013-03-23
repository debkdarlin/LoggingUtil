///////////////////////////////////////////////////////////////////////////////
// Logging Utility
// Copyright (C) 2010-2013 LoRd_MuldeR <MuldeR2@GMX.de>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
// http://www.gnu.org/licenses/gpl-2.0.txt
///////////////////////////////////////////////////////////////////////////////

#include "LogProcessor.h"

//Windows
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

//Qt
#include <QProcess>
#include <QTextStream>
#include <QTextCodec>
#include <QFile>
#include <QDateTime>
#include <QCoreApplication>
#include <QTimer>

//Const
static const int CHANNEL_STDOUT = 1;
static const int CHANNEL_STDERR = 2;
static const int CHANNEL_OTHERS = 3;

//Helper
#define SAFE_DEL(X) do { if(X) { delete (X); X = NULL; } } while (0)

/*
 * Constructor
 */
CLogProcessor::CLogProcessor(QFile &logFile)
:
	m_logStdout(true),
	m_logStderr(true),
	m_simplify(true),
	m_logIsEmpty(false),
	m_exitCode(-1)
{
	m_process = new QProcess();
	
	//Setup process
	m_process->setProcessChannelMode(QProcess::SeparateChannels);
	connect(m_process, SIGNAL(readyReadStandardOutput()), this, SLOT(readFromStdout()));
	connect(m_process, SIGNAL(readyReadStandardError()), this, SLOT(readFromStderr()));
	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processFinished(int)));

	//Create decoder
	m_codecStdout = QTextCodec::codecForName("UTF-8");
	m_codecStderr = QTextCodec::codecForName("UTF-8");

	//Setup regular exporession
	m_regExp = new QRegExp("(\\f|\\n|\\r|\\v)");

	//Assign the log file
	m_logFile = new QTextStream(&logFile);
	m_logFile->setCodec(QTextCodec::codecForName("UTF-8"));
	m_logIsEmpty = (logFile.size() == 0);


	//Create event loop
	m_eventLoop = new QEventLoop();
}

/*
 * Destructor
 */
CLogProcessor::~CLogProcessor(void)
{
	if(m_process)
	{
		m_process->kill();
		m_process->waitForFinished();
	}
	
	SAFE_DEL(m_process);
	SAFE_DEL(m_regExp);
	SAFE_DEL(m_logFile);
}

/*
 * Start the process
 */
bool CLogProcessor::startProcess(const QString &program, const QStringList &arguments)
{
	if(m_process->state() != QProcess::NotRunning)
	{
		return false;
	}

	if(!m_logIsEmpty) logString("---", CHANNEL_OTHERS);
	logString(QString("Creating new process: %1 [%2]").arg(program, arguments.join("; ")), CHANNEL_OTHERS);
	
	m_process->start(program, arguments);

	if(!m_process->waitForStarted())
	{
		logString(QString("Process creation failed: %1").arg(m_process->errorString()) , CHANNEL_OTHERS);
		m_process->kill();
		return false;
	}

	logString(QString().sprintf("Process created successfully (PID: 0x%08X)", m_process->pid()->hProcess), CHANNEL_OTHERS);
	return true;
}

/*
 * Event processing
 */
int CLogProcessor::exec(void)
{
	if(m_process->state() == QProcess::Running)
	{	
		QTimer::singleShot(0, this, SLOT(readFromStdout()));
		QTimer::singleShot(0, this, SLOT(readFromStderr()));
		return m_eventLoop->exec();
	}
	else
	{
		return m_exitCode;
	}
}

/*
 * Read from StdOut
 */
void CLogProcessor::readFromStdout(void)
{
	const QByteArray data = m_process->readAllStandardOutput();

	if(data.length() > 0)
	{
		fwrite(data.constData(), 1, data.length(), stdout);
		if(m_logStdout) processData(data, CHANNEL_STDOUT);
	}
}

/*
 * Read from StdErr
 */
void CLogProcessor::readFromStderr(void)
{
	const QByteArray data = m_process->readAllStandardError();

	if(data.length() > 0)
	{
		fwrite(data.constData(), 1, data.length(), stderr);
		if(m_logStderr) processData(data, CHANNEL_STDERR);
	}
}

/*
 * Process data (decode and tokenize)
 */
void CLogProcessor::processData(const QByteArray &data, const int channel)
{
	QString *buffer = NULL;
	QTextCodec *decoder = NULL;
	
	switch(channel)
	{
	case CHANNEL_STDOUT:
		buffer = &m_bufferStdout;
		decoder = m_codecStdout;
		break;
	case CHANNEL_STDERR:
		buffer = &m_bufferStderr;
		decoder = m_codecStderr;
		break;
	default:
		throw "Bad selection!";
	}

	buffer->append(decoder->toUnicode(data).replace(QChar('\b'), QChar('\r')));

	int pos = m_regExp->indexIn(*buffer);
	while(pos >= 0)
	{
		if(pos > 0)
		{
			logString(m_simplify ? buffer->left(pos).simplified() : buffer->left(pos), channel);
		}
		buffer->remove(0, pos + 1);
		pos = m_regExp->indexIn(*buffer);
	}
}

/*
 * Append string to log file
 */
void CLogProcessor::logString(const QString &data, const int channel)
{
	/*TODO: Implement filtering!*/

	QChar chan;
	static const QString format("yyyy-MM-dd hh:mm:ss");

	switch(channel)
	{
	case CHANNEL_STDOUT:
		chan = 'O';
		break;
	case CHANNEL_STDERR:
		chan = 'E';
		break;
	case CHANNEL_OTHERS:
		chan = 'I';
		break;
	default:
		throw "Bad selection!";
	}

	m_logFile->operator<<(QString("[%1] %2: %3\r\n").arg(chan, QDateTime::currentDateTime().toString(format), data));
}

/*
 * Process has finished
 */
void CLogProcessor::processFinished(int exitCode)
{
	m_exitCode = exitCode;
	logString(QString().sprintf("Process has terminated (Exit Code: 0x%08X)", exitCode), CHANNEL_OTHERS);
	m_eventLoop->exit(m_exitCode);
}
