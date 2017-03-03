/*  This file is part of YUView - The YUV player with advanced analytics toolset
*   <https://github.com/IENT/YUView>
*   Copyright (C) 2015  Institut für Nachrichtentechnik, RWTH Aachen University, GERMANY
*
*   This program is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 3 of the License, or
*   (at your option) any later version.
*
*   In addition, as a special exception, the copyright holders give
*   permission to link the code of portions of this program with the
*   OpenSSL library under certain conditions as described in each
*   individual source file, and distribute linked combinations including
*   the two.
*   
*   You must obey the GNU General Public License in all respects for all
*   of the code used other than OpenSSL. If you modify file(s) with this
*   exception, you may extend this exception to your version of the
*   file(s), but you are not obligated to do so. If you do not wish to do
*   so, delete this exception statement from your version. If you delete
*   this exception statement from all source files in the program, then
*   also delete it here.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "playlistItemFFmpegFile.h"

#include <QDebug>
#include <QDir>
#include <QUrl>
#include <QPainter>

#include "fileSource.h"

#define FFMPEG_DEBUG_OUTPUT 0
#if FFMPEG_DEBUG_OUTPUT && !NDEBUG
#include <QDebug>
#define DEBUG_FFMPEG qDebug
#else
#define DEBUG_FFMPEG(fmt,...) ((void)0)
#endif

playlistItemFFmpegFile::playlistItemFFmpegFile(const QString &ffmpegFilePath)
  : playlistItemWithVideo(ffmpegFilePath, playlistItem_Indexed)
{
  // Set the properties of the playlistItem
  setIcon(0, QIcon(":img_videoHEVC.png"));
  setFlags(flags() | Qt::ItemIsDropEnabled);

  // Set the video pointer correctly
  video.reset(new videoHandlerYUV());

  // Connect the basic signals from the video
  playlistItemWithVideo::connectVideo();

  // Open the file
  if (!loadingDecoder.openFile(ffmpegFilePath))
  {
    // Opening the input file failed.
    decoderReady = false;
    return;
  }

  if (!cachingDecoder.openFile(ffmpegFilePath))
  {
    // Opening the input file failed.
    decoderReady = false;
    return;
  }

  // The FFMpeg file can be cached.
  cachingEnabled = true;

  // Set the frame number limits
  startEndFrame = getStartEndFrameLimits();

  // Load YUV data fro frame 0
  loadYUVData(0, false);

  videoHandlerYUV *yuvVideo = dynamic_cast<videoHandlerYUV*>(video.data());
  connect(yuvVideo, &videoHandlerYUV::signalRequestRawData, this, &playlistItemFFmpegFile::loadYUVData, Qt::DirectConnection);
  connect(yuvVideo, &videoHandlerYUV::signalUpdateFrameLimits, this, &playlistItemFFmpegFile::slotUpdateFrameLimits);
}

void playlistItemFFmpegFile::getSupportedFileExtensions(QStringList &allExtensions, QStringList &filters)
{
  QStringList ext;
  ext << "avi" << "avr" << "cdxl" << "xl" << "dv" << "dif" << "flm" << "flv" << "flv" << "h261" << "h26l" << "h264" << "264" << "avc" << "cgi" << "ivr" << "lvf" << "m4v" << "mkv" << "mk3d" << "mka" << "mks" << "mjpg" << "mjpeg" << "mpo" << "j2k" << "mov" << "mp4" << "m4a" << "3gp" << "3g2" << "mj2" << "mvi" << "mxg" << "v" << "ogg" << "mjpg" << "viv" << "xmv";
  QString filtersString = "FFMpeg files (";
  for (QString e : ext)
    filtersString.append(QString("*.%1").arg(e));
  filtersString.append(")");

  allExtensions.append(ext);
  filters.append(filtersString);
}

playlistItemFFmpegFile *playlistItemFFmpegFile::newplaylistItemFFmpegFile(const QDomElementYUView &root, const QString &playlistFilePath)
{
  // Parse the DOM element. It should have all values of a playlistItemHEVCFile
  QString absolutePath = root.findChildValue("absolutePath");
  QString relativePath = root.findChildValue("relativePath");

  // check if file with absolute path exists, otherwise check relative path
  QString filePath = fileSource::getAbsPathFromAbsAndRel(playlistFilePath, absolutePath, relativePath);
  if (filePath.isEmpty())
    return nullptr;

  // We can still not be sure that the file really exists, but we gave our best to try to find it.
  playlistItemFFmpegFile *newFile = new playlistItemFFmpegFile(filePath);

  // Load the propertied of the playlistItemIndexed
  playlistItem::loadPropertiesFromPlaylist(root, newFile);

  return newFile;
}

void playlistItemFFmpegFile::savePlaylist(QDomElement &root, const QDir &playlistDir) const
{
  // Determine the relative path to the HEVC file. We save both in the playlist.
  QUrl fileURL(plItemNameOrFileName);
  fileURL.setScheme("file");
  QString relativePath = playlistDir.relativeFilePath(plItemNameOrFileName);

  QDomElementYUView d = root.ownerDocument().createElement("playlistItemHEVCFile");

  // Append the properties of the playlistItem
  playlistItem::appendPropertiesToPlaylist(d);

  // Append all the properties of the HEVC file (the path to the file. Relative and absolute)
  d.appendProperiteChild("absolutePath", fileURL.toString());
  d.appendProperiteChild("relativePath", relativePath);

  root.appendChild(d);
}

infoData playlistItemFFmpegFile::getInfo() const
{
  infoData info("FFMpeg File Info");

  // At first append the file information part (path, date created, file size...)
  info.items.append(loadingDecoder.getFileInfoList());

  if (!decoderReady)
    info.items.append(infoItem("Error", "Opening the file failed."));
  if (loadingDecoder.errorInDecoder())
    info.items.append(infoItem("Error", loadingDecoder.decoderErrorString()));
  else
  {
    QSize videoSize = video->getFrameSize();
    info.items.append(infoItem("Resolution", QString("%1x%2").arg(videoSize.width()).arg(videoSize.height()), "The video resolution in pixel (width x height)"));
    info.items.append(infoItem("Num Frames", QString::number(loadingDecoder.getNumberPOCs()), "The number of pictures in the stream."));
  }

  return info;
}

void playlistItemFFmpegFile::loadYUVData(int frameIdx, bool caching)
{
  if (caching && !cachingEnabled)
    return;

  if (!decoderReady)
    // We can not decode images
    return;

  DEBUG_FFMPEG("playlistItemFFmpegFile::loadYUVData %d %s", frameIdx, caching ? "caching" : "");

  videoHandlerYUV *yuvVideo = dynamic_cast<videoHandlerYUV*>(video.data());
  yuvVideo->setFrameSize(loadingDecoder.getFrameSize());
  yuvVideo->setYUVPixelFormat(loadingDecoder.getYUVPixelFormat());
  
  if (frameIdx > startEndFrame.second || frameIdx < 0)
  {
    DEBUG_FFMPEG("playlistItemHEVCFile::loadYUVData Invalid frame index");
    return;
  }

  // Just get the frame from the correct decoder
  QByteArray decByteArray;

  if (caching)
    decByteArray = cachingDecoder.loadYUVFrameData(frameIdx);
  else
    decByteArray = loadingDecoder.loadYUVFrameData(frameIdx);
  
  if (!decByteArray.isEmpty())
  {
    yuvVideo->rawYUVData = decByteArray;
    yuvVideo->rawYUVData_frameIdx = frameIdx;
  }
}

void playlistItemFFmpegFile::createPropertiesWidget( )
{
  // Absolutely always only call this once
  assert(!propertiesWidget);

  preparePropertiesWidget(QStringLiteral("playlistItemHEVCFile"));

  // On the top level everything is layout vertically
  QVBoxLayout *vAllLaout = new QVBoxLayout(propertiesWidget.data());

  QFrame *lineOne = new QFrame;
  lineOne->setObjectName(QStringLiteral("line"));
  lineOne->setFrameShape(QFrame::HLine);
  lineOne->setFrameShadow(QFrame::Sunken);

  // First add the parents controls (first index controllers (start/end...) then YUV controls (format,...)
  videoHandlerYUV *yuvVideo = dynamic_cast<videoHandlerYUV*>(video.data());
  vAllLaout->addLayout(createPlaylistItemControls());
  vAllLaout->addWidget(lineOne);
  vAllLaout->addLayout(yuvVideo->createYUVVideoHandlerControls(true));

  // Insert a stretch at the bottom of the vertical global layout so that everything
  // gets 'pushed' to the top.
  vAllLaout->insertStretch(5, 1);
}

ValuePairListSets playlistItemFFmpegFile::getPixelValues(const QPoint &pixelPos, int frameIdx)
{
  // TODO: This could also be RGB
  ValuePairListSets newSet;
  newSet.append("YUV", video->getPixelValues(pixelPos, frameIdx));
  return newSet;
}

void playlistItemFFmpegFile::reloadItemSource()
{
  // TODO: The caching decoder must also be reloaded
  //       All items in the cache are also now invalid

  loadingDecoder.reloadItemSource();

  // Set the frame number limits
  startEndFrame = getStartEndFrameLimits();

  // Reset the videoHandlerYUV source. With the next draw event, the videoHandlerYUV will request to decode the frame again.
  video->invalidateAllBuffers();

  // Load frame 0. This will decode the first frame in the sequence and set the
  // correct frame size/YUV format.
  loadYUVData(0, false);
}

void playlistItemFFmpegFile::cacheFrame(int idx)
{
  if (!cachingEnabled)
    return;

  // Cache a certain frame. This is always called in a separate thread.
  cachingMutex.lock();
  video->cacheFrame(idx);
  cachingMutex.unlock();
}

void playlistItemFFmpegFile::loadFrame(int frameIdx, bool playing, bool loadRawdata)
{
  auto stateYUV = video->needsLoading(frameIdx, loadRawdata);
  
  if (stateYUV == LoadingNeeded)
  {
    isFrameLoading = true;
    
    // Load the requested current frame
    DEBUG_FFMPEG("playlistItemRawFile::loadFrame loading frame %d %s", frameIdx, playing ? "(playing)" : "");
    video->loadFrame(frameIdx);
    
    isFrameLoading = false;
    emit signalItemChanged(true);
  }

  if (playing && (stateYUV == LoadingNeeded || stateYUV == LoadingNeededDoubleBuffer))
  {
    // Load the next frame into the double buffer
    int nextFrameIdx = frameIdx + 1;
    if (nextFrameIdx <= startEndFrame.second)
    {
      DEBUG_FFMPEG("playlistItemRawFile::loadFrame loading frame into double buffer %d %s", nextFrameIdx, playing ? "(playing)" : "");
      isFrameLoadingDoubleBuffer = true;
      video->loadFrame(nextFrameIdx, true);
      isFrameLoadingDoubleBuffer = false;
      emit signalItemDoubleBufferLoaded();
    }
  }
}
