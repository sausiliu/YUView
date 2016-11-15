/*  YUView - YUV player with advanced analytics toolset
*   Copyright (C) 2015  Institut für Nachrichtentechnik
*                       RWTH Aachen University, GERMANY
*
*   YUView is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   YUView is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with YUView.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "playlistItemImageFile.h"

#include "fileSource.h"

#include <QPainter>
#include <QImageReader>
#include <QSettings>
#include <QtConcurrent>

#define IMAGEFILE_ERROR_TEXT "The given image file could not be laoaded."
#define IMAGEFILE_LOADING_TEXT "Loading image ..."

playlistItemImageFile::playlistItemImageFile(const QString &filePath) : playlistItem(filePath, playlistItem_Static)
{
  // Set the properties of the playlistItem
  setIcon(0, QIcon(":img_television.png"));
  // Nothing can be dropped onto an image file
  setFlags(flags() & ~Qt::ItemIsDropEnabled);

  // The image file is unchanged
  fileChanged = false;

  // Does the file exits?
  QFileInfo fileInfo(filePath);
  if (!fileInfo.exists() || !fileInfo.isFile())
    return;

  connect(&fileWatcher, SIGNAL(fileChanged(const QString)), this, SLOT(fileSystemWatcherFileChanged(const QString)));

  // Install a file watcher if file watching is active.
  updateFileWatchSetting();

  // Open the file in the background
  backgroundLoadingFuture = QtConcurrent::run(this, &playlistItemImageFile::backgroundLoadImage);
}

void playlistItemImageFile::backgroundLoadImage()
{
  if (!frame.loadCurrentImageFromFile(plItemNameOrFileName))
    return;

  emit signalItemChanged(true, false);
}

void playlistItemImageFile::savePlaylist(QDomElement &root, const QDir &playlistDir) const
{
  // Determine the relative path to the raw file. We save both in the playlist.
  QUrl fileURL(plItemNameOrFileName);
  fileURL.setScheme("file");
  QString relativePath = playlistDir.relativeFilePath(plItemNameOrFileName);

  QDomElementYUView d = root.ownerDocument().createElement("playlistItemImageFile");

  // Append the properties of the playlistItem
  playlistItem::appendPropertiesToPlaylist(d);
  
  // Apppend all the properties of the raw file (the path to the file. Relative and absolute)
  d.appendProperiteChild("absolutePath", fileURL.toString());
  d.appendProperiteChild("relativePath", relativePath);
  
  root.appendChild(d);
}

/* Parse the playlist and return a new playlistItemImageFile.
*/
playlistItemImageFile *playlistItemImageFile::newplaylistItemImageFile(const QDomElementYUView &root, const QString &playlistFilePath)
{
  // Parse the dom element. It should have all values of a playlistItemImageFile
  QString absolutePath = root.findChildValue("absolutePath");
  QString relativePath = root.findChildValue("relativePath");
  
  // check if file with absolute path exists, otherwise check relative path
  QString filePath = fileSource::getAbsPathFromAbsAndRel(playlistFilePath, absolutePath, relativePath);
  if (filePath.isEmpty())
    return NULL;

  playlistItemImageFile *newImage = new playlistItemImageFile(filePath);
  
  // Load the propertied of the playlistItemIndexed
  playlistItem::loadPropertiesFromPlaylist(root, newImage);
  
  return newImage;
}

void playlistItemImageFile::drawItem(QPainter *painter, int frameIdx, double zoomFactor, bool playback)
{
  Q_UNUSED(frameIdx);
  Q_UNUSED(playback);

  if (!frame.isFormatValid() || backgroundLoadingFuture.isRunning())
  {
    // The image could not be loaded or is being loaded right now. Draw this as text instead.
    QString text = backgroundLoadingFuture.isRunning() ? IMAGEFILE_LOADING_TEXT : IMAGEFILE_ERROR_TEXT;

    // Get the size of the text and create a rect of that size which is centered at (0,0)
    QFont displayFont = painter->font();
    displayFont.setPointSizeF(painter->font().pointSizeF() * zoomFactor);
    painter->setFont(displayFont);
    QSize textSize = painter->fontMetrics().size(0, text);
    QRect textRect;
    textRect.setSize(textSize);
    textRect.moveCenter(QPoint(0,0));

    // Draw the text
    painter->drawText(textRect, text);
  }
  else
    // Draw the frame
    frame.drawFrame(painter, zoomFactor);
}

void playlistItemImageFile::getSupportedFileExtensions(QStringList &allExtensions, QStringList &filters)
{
  const QList<QByteArray> formats = QImageReader::supportedImageFormats();

  QString filter = "Static Image (";
  for (auto &fmt : formats)
  {
    QString formatString = QString(fmt);
    allExtensions.append(formatString);
    filter += "*." + formatString + " ";
  }

  if (filter.endsWith(' '))
    filter.chop(1);

  filter += ")";

  filters.append(filter);
}

ValuePairListSets playlistItemImageFile::getPixelValues(const QPoint &pixelPos, int frameIdx)
{
  ValuePairListSets newSet;
  newSet.append("RGB", frame.getPixelValues(pixelPos, frameIdx));
  return newSet;
}

QList<infoItem> playlistItemImageFile::getInfoList() const
{
  QList<infoItem> infoList;

  infoList.append(infoItem("File", plItemNameOrFileName));
  if (frame.isFormatValid())
  {
    QSize frameSize = frame.getFrameSize();
    infoList.append(infoItem("Resolution", QString("%1x%2").arg(frameSize.width()).arg(frameSize.height()), "The video resolution in pixel (width x height)"));
    QImage img = frame.getCurrentFrameAsImage();
    infoList.append(infoItem("Bit depth", QString::number(img.depth()), "The bit depth of the image."));
  }
  else if (backgroundLoadingFuture.isRunning())
    infoList.append(infoItem("Status", "Loading...", "The image is being loaded. Please wait."));
  else
    infoList.append(infoItem("Status", "Error", "There was an error loading the image."));

  return infoList;
}

void playlistItemImageFile::reloadItemSource()
{
  // Reload the frame in the background
  backgroundLoadingFuture = QtConcurrent::run(this, &playlistItemImageFile::backgroundLoadImage);
}

void playlistItemImageFile::updateFileWatchSetting()
{
  // Install a file watcher if file watching is active in the settings.
  // The addPath/removePath functions will do nothing if called twice for the same file.
  QSettings settings;
  if (settings.value("WatchFiles",true).toBool())
    fileWatcher.addPath(plItemNameOrFileName);
  else
    fileWatcher.removePath(plItemNameOrFileName);
}
