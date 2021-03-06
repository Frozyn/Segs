/*
 * SEGS - Super Entity Game Server
 * http://www.segs.io/
 * Copyright (c) 2006 - 2018 SEGS Team (see AUTHORS.md)
 * This software is licensed under the terms of the 3-clause BSD License. See LICENSE.md for details.
 */

/*!
 * @addtogroup MapViewer Projects/CoX/Utilities/MapViewer
 * @{
 */

#include "PiggTools.h"

#include <QtCore/QDir>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>

#include <vector>
#include <cassert>

namespace {
#pragma pack(push, 1)

struct PiggHeader
{
    int pigg_magic;
    int16_t unused;
    int16_t version;
    int16_t header_size;
    int16_t used_header_bytes;
    uint32_t num_entries;
};

struct PiggInternalHeader
{
    int flag;
    int name_id;
    uint32_t size;
    uint32_t ftime;
    uint32_t offset;
    int unused[6];
    uint32_t packed_size;
};

#pragma pack(pop)
struct Pigg_DataTable
{
    int datapool_flag;
    std::vector<QString> data_parts;
};

struct PiggFile
{
   PiggHeader hdr;
   std::vector<PiggInternalHeader> headers;
   Pigg_DataTable strings_table;
   QString fname;
};

template<class POD>
bool readPOD(QIODevice &src,POD &tgt)
{
    return sizeof(POD)==src.read((char *)&tgt,sizeof(POD));
}

//Unpack the ZIP data using qt functions, need to prepend the uncompressed data size to the source data.
QByteArray uncompr_zip(QByteArray &compressed_data,uint32_t size_uncom)
{
    compressed_data.prepend( char((size_uncom >> 0) & 0xFF));
    compressed_data.prepend( char((size_uncom >> 8) & 0xFF));
    compressed_data.prepend( char((size_uncom >> 16) & 0xFF));
    compressed_data.prepend( char((size_uncom >> 24) & 0xFF));
    return qUncompress(compressed_data);
}

bool loadDataTable(QFile &src,Pigg_DataTable &target)
{
    int table_sizes;
    int num_entries;

    if(!readPOD(src,target.datapool_flag))
        return false;
    if(!readPOD(src,table_sizes))
        return false;
    if(!readPOD(src,num_entries))
        return false;
    while(num_entries>0) {
        uint32_t entrysize;
        if(!readPOD(src,entrysize))
            return false;
        if(entrysize>0)
            target.data_parts.push_back(QString::fromLocal8Bit(src.read(entrysize)));
        num_entries -= entrysize+4;
    }
    return true;
}

bool loadPigg(const QString &fname,PiggFile &pigg)
{
    QFile src_fl(fname);
    if(!src_fl.open(QFile::ReadOnly))
        return false;
    pigg.fname = fname;
    if(false==readPOD(src_fl,pigg.hdr)) {
        qDebug() << "file too short";
        return false;
    }
    if(pigg.hdr.pigg_magic!=0x123) {
        qDebug() << "Bad juju!";
        return false;
    }
    if (pigg.hdr.version > 2)
    {
        qDebug() << "File" <<fname<< "has bad pigg version"<<pigg.hdr.version;
        return  false;
    }
    if (pigg.hdr.header_size != 0x10)
        src_fl.seek(src_fl.pos()+pigg.hdr.header_size - 0x10);
    pigg.headers.resize(pigg.hdr.num_entries);
    assert(pigg.hdr.used_header_bytes==0x30);
    for(PiggInternalHeader &in_hdr : pigg.headers ) {
        readPOD(src_fl,in_hdr);
    }
    int idx=0;
    for(PiggInternalHeader &in_hdr : pigg.headers )
    {
        if (in_hdr.flag != 0x3456)
        {
            qDebug() << "Bad flag on fileheader #"<<idx;
            return false;
        }
        uint32_t internal_fsize = in_hdr.packed_size ? in_hdr.packed_size : in_hdr.size;
        if (in_hdr.offset + internal_fsize - 1 > src_fl.size())
        {
            qDebug() << "File larger than available pigg data - probably corrupt .pigg file!  #"<<idx;
            return false;
        }
        if (in_hdr.name_id < 0)
        {
            qDebug() << "Name id is invalid in #"<<idx;
            return false;
        }
        ++idx;
    }
    loadDataTable(src_fl,pigg.strings_table);
    Pigg_DataTable headers_table; // 'headers' table is not used/needed during file listing/extraction operations
    loadDataTable(src_fl,headers_table);
    if (pigg.strings_table.datapool_flag != 0x6789)
    {
        qDebug() << "Bad string table magic";
        return false;
    }
    if (headers_table.datapool_flag != 0x9ABC) {
        qDebug() << "Bad headers table magic";
        return false;
    }

    qDebug() << "Header loaded";
    return true;
}

bool saveFile(const QString &fname,const QByteArray &data)
{
    QFile tgt_fl(fname);
    if(!tgt_fl.open(QFile::WriteOnly)) {
        qWarning() << "failed to open target file" << fname;
        return false;
    }
    if(data.size()!=tgt_fl.write(data)) {
        qWarning() << "Failed to write complete file - write error";
        return false;
    }
    return true;

}

bool extractAllFiles(PiggFile &pigg,const QString &tgt_path)
{
    QFile src_fl(pigg.fname);
    if(!src_fl.open(QFile::ReadOnly))
    {
        qWarning() << "failed to open source file" << pigg.fname;
        return false;
    }
    const QDir curdir(tgt_path);
    for(const PiggInternalHeader & ih : pigg.headers)
    {
        const QString target_fname=pigg.strings_table.data_parts[ih.name_id];
        if(qApp)
            qApp->processEvents();
        else
            qDebug().noquote() << "Extracting"<<target_fname;
        src_fl.seek(ih.offset);
        bool was_packed = ih.packed_size!=0;
        QByteArray src_data = src_fl.read(was_packed ? ih.packed_size : ih.size);
        const QFileInfo fi(target_fname);
        if(!curdir.exists(fi.path()))
            curdir.mkpath(fi.path());
        if(was_packed) {
            QByteArray actual_data = uncompr_zip(src_data,ih.size);
            src_data = actual_data;
        }
        if(!saveFile(curdir.filePath(target_fname),src_data))
            return false;
    }
    return true;
}
} // namespace

bool unpackPiggFile(const QString &filename,const QString &target_path)
{
    PiggFile hdr;
    if(!loadPigg(filename,hdr))
        return false;

    return extractAllFiles(hdr,target_path);
}

//! @}
