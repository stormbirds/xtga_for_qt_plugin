/* This file is part of the KDE project
   Copyright (C) 2003 Dominik Seichter <domseichter@web.de>
   Copyright (C) 2004 Ignacio Castaño <castano@ludicon.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the Lesser GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
*/

/* this code supports:
 * reading:
 *     uncompressed and run length encoded indexed, grey and color tga files.
 *     image types 1, 2, 3, 9, 10 and 11.
 *     only RGB color maps with no more than 256 colors.
 *     pixel formats 8, 16, 24 and 32.
 * writing:
 *     uncompressed true color tga files
 */

#include "tga_p.h"

#include <assert.h>

#include <QImage>
#include <QDataStream>
#include <QDebug>

typedef quint32 uint;
typedef quint16 ushort;
typedef quint8 uchar;

namespace   // Private.
{

static quint32 TargaError;

// Header format of saved files.
uchar targaMagic[12] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

#define TGA_IMG_NODATA             (0)
#define TGA_IMG_UNC_PALETTED       (1)
#define TGA_IMG_UNC_TRUECOLOR      (2)
#define TGA_IMG_UNC_GRAYSCALE      (3)
#define TGA_IMG_RLE_PALETTED       (9)
#define TGA_IMG_RLE_TRUECOLOR      (10)
#define TGA_IMG_RLE_GRAYSCALE      (11)


#define TGA_LOWER_LEFT             (0)
#define TGA_LOWER_RIGHT            (1)
#define TGA_UPPER_LEFT             (2)
#define TGA_UPPER_RIGHT            (3)


#define HDR_LENGTH               (18)
#define HDR_IDLEN                (0)
#define HDR_CMAP_TYPE            (1)
#define HDR_IMAGE_TYPE           (2)
#define HDR_CMAP_FIRST           (3)
#define HDR_CMAP_LENGTH          (5)
#define HDR_CMAP_ENTRY_SIZE      (7)
#define HDR_IMG_SPEC_XORIGIN     (8)
#define HDR_IMG_SPEC_YORIGIN     (10)
#define HDR_IMG_SPEC_WIDTH       (12)
#define HDR_IMG_SPEC_HEIGHT      (14)
#define HDR_IMG_SPEC_PIX_DEPTH   (16)
#define HDR_IMG_SPEC_IMG_DESC    (17)



#define TGA_ERR_NONE                    (0)
#define TGA_ERR_BAD_HEADER              (1)
#define TGA_ERR_OPEN_FAILS              (2)
#define TGA_ERR_BAD_FORMAT              (3)
#define TGA_ERR_UNEXPECTED_EOF          (4)
#define TGA_ERR_NODATA_IMAGE            (5)
#define TGA_ERR_COLORMAP_FOR_GRAY       (6)
#define TGA_ERR_BAD_COLORMAP_ENTRY_SIZE (7)
#define TGA_ERR_BAD_COLORMAP            (8)
#define TGA_ERR_READ_FAILS              (9)
#define TGA_ERR_BAD_IMAGE_TYPE          (10)
#define TGA_ERR_BAD_DIMENSIONS          (11)


enum TGAType {
    TGA_TYPE_INDEXED        = 1,
    TGA_TYPE_RGB            = 2,
    TGA_TYPE_GREY           = 3,
    TGA_TYPE_RLE_INDEXED    = 9,
    TGA_TYPE_RLE_RGB        = 10,
    TGA_TYPE_RLE_GREY       = 11
};

#define TGA_INTERLEAVE_MASK 0xc0
#define TGA_INTERLEAVE_NONE 0x00
#define TGA_INTERLEAVE_2WAY 0x40
#define TGA_INTERLEAVE_4WAY 0x80

#define TGA_ORIGIN_MASK     0x30
#define TGA_ORIGIN_LEFT     0x00
#define TGA_ORIGIN_RIGHT    0x10
#define TGA_ORIGIN_LOWER    0x00
#define TGA_ORIGIN_UPPER    0x20

/** Tga Header. */
struct TgaHeader {
    uchar id_length;
    uchar colormap_type;
    uchar image_type;
    ushort colormap_index;
    ushort colormap_length;
    uchar colormap_size;
    ushort x_origin;
    ushort y_origin;
    ushort width;
    ushort height;
    uchar pixel_size;
    uchar flags;

    enum { SIZE = 18 }; // const static int SIZE = 18;
};

static QDataStream &operator>> (QDataStream &s, TgaHeader &head)
{
    s >> head.id_length;
    s >> head.colormap_type;
    s >> head.image_type;
    s >> head.colormap_index;
    s >> head.colormap_length;
    s >> head.colormap_size;
    s >> head.x_origin;
    s >> head.y_origin;
    s >> head.width;
    s >> head.height;
    s >> head.pixel_size;
    s >> head.flags;
    /*qDebug() << "id_length: " << head.id_length << " - colormap_type: " << head.colormap_type << " - image_type: " << head.image_type;
    qDebug() << "colormap_index: " << head.colormap_index << " - colormap_length: " << head.colormap_length << " - colormap_size: " << head.colormap_size;
    qDebug() << "x_origin: " << head.x_origin << " - y_origin: " << head.y_origin << " - width:" << head.width << " - height:" << head.height << " - pixelsize: " << head.pixel_size << " - flags: " << head.flags;*/
    return s;
}

static bool IsSupported(const TgaHeader &head)
{
    if (head.image_type != TGA_TYPE_INDEXED &&
            head.image_type != TGA_TYPE_RGB &&
            head.image_type != TGA_TYPE_GREY &&
            head.image_type != TGA_TYPE_RLE_INDEXED &&
            head.image_type != TGA_TYPE_RLE_RGB &&
            head.image_type != TGA_TYPE_RLE_GREY) {
        return false;
    }
    if (head.image_type == TGA_TYPE_INDEXED ||
            head.image_type == TGA_TYPE_RLE_INDEXED) {
        if (head.colormap_length > 256 || head.colormap_size != 24 || head.colormap_type != 1) {
            return false;
        }
    }
    if (head.image_type == TGA_TYPE_RGB ||
            head.image_type == TGA_TYPE_GREY ||
            head.image_type == TGA_TYPE_RLE_RGB ||
            head.image_type == TGA_TYPE_RLE_GREY) {
        if (head.colormap_type != 0) {
            return false;
        }
    }
    if (head.width == 0 || head.height == 0) {
        return false;
    }
    if (head.pixel_size != 8 && head.pixel_size != 16 &&
            head.pixel_size != 24 && head.pixel_size != 32) {
        return false;
    }
    return true;
}

struct Color555 {
    ushort b : 5;
    ushort g : 5;
    ushort r : 5;
};

struct TgaHeaderInfo {
    bool rle;
    bool pal;
    bool rgb;
    bool grey;

    TgaHeaderInfo(const TgaHeader &tga) : rle(false), pal(false), rgb(false), grey(false)
    {
        switch (tga.image_type) {
        case TGA_TYPE_RLE_INDEXED:
            rle = true;
        Q_FALLTHROUGH();
        // no break is intended!
        case TGA_TYPE_INDEXED:
            pal = true;
            break;

        case TGA_TYPE_RLE_RGB:
            rle = true;
        Q_FALLTHROUGH();
        // no break is intended!
        case TGA_TYPE_RGB:
            rgb = true;
            break;

        case TGA_TYPE_RLE_GREY:
            rle = true;
        Q_FALLTHROUGH();
        // no break is intended!
        case TGA_TYPE_GREY:
            grey = true;
            break;

        default:
            // Error, unknown image type.
            break;
        }
    }
};

static bool LoadTGA(QDataStream &s, const TgaHeader &tga, QImage &img)
{
    // Create image.
    img = QImage(tga.width, tga.height, QImage::Format_RGB32);

    TgaHeaderInfo info(tga);

    // Bits 0-3 are the numbers of alpha bits (can be zero!)
    const int numAlphaBits = tga.flags & 0xf;
    // However alpha exists only in the 32 bit format.
    if ((tga.pixel_size == 32) && (tga.flags & 0xf)) {
        img = QImage(tga.width, tga.height, QImage::Format_ARGB32);

        if (numAlphaBits > 8) {
            return false;
        }
    }

    uint pixel_size = (tga.pixel_size / 8);
    qint64 size = qint64(tga.width) * qint64(tga.height) * pixel_size;

    if (size < 1) {
//          qDebug() << "This TGA file is broken with size " << size;
        return false;
    }

    // Read palette.
    static const int max_palette_size = 768;
    char palette[max_palette_size];
    if (info.pal) {
        // @todo Support palettes in other formats!
        const int palette_size = 3 * tga.colormap_length;
        if (palette_size > max_palette_size) {
            return false;
        }
        const int dataRead = s.readRawData(palette, palette_size);
        if (dataRead < 0) {
            return false;
        }
        if (dataRead < max_palette_size) {
            memset(&palette[dataRead], 0, max_palette_size - dataRead);
        }
    }

    // Allocate image.
    uchar *const image = reinterpret_cast<uchar*>(malloc(size));
    if (!image) {
        return false;
    }

    bool valid = true;

    if (info.rle) {
        // Decode image.
        char *dst = (char *)image;
        qint64 num = size;

        while (num > 0) {
            if (s.atEnd()) {
                valid = false;
                break;
            }

            // Get packet header.
            uchar c;
            s >> c;

            uint count = (c & 0x7f) + 1;
            num -= count * pixel_size;
            if (num < 0) {
                valid = false;
                break;
            }

            if (c & 0x80) {
                // RLE pixels.
                assert(pixel_size <= 8);
                char pixel[8];
                const int dataRead = s.readRawData(pixel, pixel_size);
                if (dataRead < (int)pixel_size) {
                    memset(&pixel[dataRead], 0, pixel_size - dataRead);
                }
                do {
                    memcpy(dst, pixel, pixel_size);
                    dst += pixel_size;
                } while (--count);
            } else {
                // Raw pixels.
                count *= pixel_size;
                const int dataRead = s.readRawData(dst, count);
                if (dataRead < 0) {
                    free(image);
                    return false;
                }
                if ((uint)dataRead < count) {
                    memset(&dst[dataRead], 0, count - dataRead);
                }
                dst += count;
            }
        }
    } else {
        // Read raw image.
        const int dataRead = s.readRawData((char *)image, size);
        if (dataRead < 0) {
            free(image);
            return false;
        }
        if (dataRead < size) {
            memset(&image[dataRead], 0, size - dataRead);
        }
    }

    if (!valid) {
        free(image);
        return false;
    }

    // Convert image to internal format.
    int y_start, y_step, y_end;
    if (tga.flags & TGA_ORIGIN_UPPER) {
        y_start = 0;
        y_step = 1;
        y_end = tga.height;
    } else {
        y_start = tga.height - 1;
        y_step = -1;
        y_end = -1;
    }

    uchar *src = image;

    for (int y = y_start; y != y_end; y += y_step) {
        QRgb *scanline = (QRgb *) img.scanLine(y);

        if (info.pal) {
            // Paletted.
            for (int x = 0; x < tga.width; x++) {
                uchar idx = *src++;
                scanline[x] = qRgb(palette[3 * idx + 2], palette[3 * idx + 1], palette[3 * idx + 0]);
            }
        } else if (info.grey) {
            // Greyscale.
            for (int x = 0; x < tga.width; x++) {
                scanline[x] = qRgb(*src, *src, *src);
                src++;
            }
        } else {
            // True Color.
            if (tga.pixel_size == 16) {
                for (int x = 0; x < tga.width; x++) {
                    Color555 c = *reinterpret_cast<Color555 *>(src);
                    scanline[x] = qRgb((c.r << 3) | (c.r >> 2), (c.g << 3) | (c.g >> 2), (c.b << 3) | (c.b >> 2));
                    src += 2;
                }
            } else if (tga.pixel_size == 24) {
                for (int x = 0; x < tga.width; x++) {
                    scanline[x] = qRgb(src[2], src[1], src[0]);
                    src += 3;
                }
            } else if (tga.pixel_size == 32) {
                for (int x = 0; x < tga.width; x++) {
                    // ### TODO: verify with images having really some alpha data
                    const uchar alpha = (src[3] << (8 - numAlphaBits));
                    scanline[x] = qRgba(src[2], src[1], src[0], alpha);
                    src += 4;
                }
            }
        }
    }

    // Free image.
    free(image);

    return true;
}

} // namespace

TGAHandler::TGAHandler()
{
}

bool TGAHandler::canRead() const
{
    if (canRead(device())) {
        setFormat("tga");
        return true;
    }
    return false;
}

bool TGAHandler::read(QImage *outImage)
{
    //qDebug() << "Loading TGA file!";

    QDataStream s(device());
    s.setByteOrder(QDataStream::LittleEndian);

    // Read image header.
    TgaHeader tga;
    s >> tga;
    s.device()->seek(TgaHeader::SIZE + tga.id_length);

    // Check image file format.
    if (s.atEnd()) {
//         qDebug() << "This TGA file is not valid.";
        return false;
    }

    // Check supported file types.
    if (!IsSupported(tga)) {
//         qDebug() << "This TGA file is not supported.";
        return false;
    }

    QImage img;
    bool result = LoadTGA(s, tga, img);

    if (result == false) {
//         qDebug() << "Error loading TGA file.";
        return false;
    }

    *outImage = img;
    return true;
}

//bool TGAHandler::write(const QImage &image)
//{
//    QDataStream s(device());
//    s.setByteOrder(QDataStream::LittleEndian);

//    const QImage &img = image;
//    const bool hasAlpha = (img.format() == QImage::Format_ARGB32);
//    for (int i = 0; i < 12; i++) {
//        s << targaMagic[i];
//    }

//    // write header
//    s << quint16(img.width());   // width
//    s << quint16(img.height());   // height
//    s << quint8(hasAlpha ? 32 : 24);   // depth (24 bit RGB + 8 bit alpha)
//    s << quint8(hasAlpha ? 0x24 : 0x20);   // top left image (0x20) + 8 bit alpha (0x4)

//    for (int y = 0; y < img.height(); y++)
//        for (int x = 0; x < img.width(); x++) {
//            const QRgb color = img.pixel(x, y);
//            s << quint8(qBlue(color));
//            s << quint8(qGreen(color));
//            s << quint8(qRed(color));
//            if (hasAlpha) {
//                s << quint8(qAlpha(color));
//            }
//        }

//    return true;
//}

bool TGAHandler::write(const QImage &image)
{
    QDataStream tga(device());
    tga.setByteOrder(QDataStream::LittleEndian);

    int width = image.width();
    int height = image.height();
    unsigned int format = 3;
    char numberOc[format];
    char numberNc[format];

    const bool hasAlpha = (image.format() == QImage::Format_ARGB32);
    int length = width * height * (hasAlpha?4:3);
    unsigned char dat [length];
    int rawNum = 0;

    for (int y = 0; y < height; y++)
       for (int x = 0; x < width; x++) {
           const QRgb color = image.pixel(x, height-1-y);
           dat[rawNum] = quint8(qRed(color));
           rawNum++;
           dat[rawNum] = quint8(qGreen(color));
           rawNum++;
           dat[rawNum] = quint8(qBlue(color));
           rawNum++;
           if (hasAlpha) {
               dat[rawNum] = quint8(qAlpha(color));
               rawNum++;
           }
       }

    quint32 i, j;
    quint32 oc, nc;

    enum RLE_STATE { INIT, NONE, RLP, RAWP };

    int state = INIT;


    quint32 size = width * height;

    quint16 shortwidth = (quint16)width;
    quint16 shortheight = (quint16)height;

    quint8 repcount;

    float red, green, blue, alpha;

    int idx, row, column;

    // have to buffer a whole line for raw packets.
    unsigned char * rawbuf = (unsigned char *)malloc( width * format );

    char id[] = "written with libtarga";
    quint8 idlen = 0;
    quint8 zeroes[5] = { 0, 0, 0, 0, 0 };
    quint32 pixbuf;
    quint8 one = 1;
    quint8 cmap_type = 0;
    quint8 img_type  = 10;  // 2 - uncompressed truecolor  10 - RLE truecolor
    quint16 xorigin  = 0;
    quint16 yorigin  = 0;
    quint8  pixdepth = format * 8;  // bpp
    quint8 img_desc  = format == TGA_TRUECOLOR_32 ? 8 : 0;


    switch( format ) {
    case TGA_TRUECOLOR_24:
    case TGA_TRUECOLOR_32:
        break;

    default:
        TargaError = TGA_ERR_BAD_FORMAT;
        return( 0 );
    }


    // write id length
//    fwrite( &idlen, 1, 1, tga );
    tga<<(idlen);

    // write colormap type
//    fwrite( &cmap_type, 1, 1, tga );
    tga<<(cmap_type);

    // write image type
//    fwrite( &img_type, 1, 1, tga );
    tga<<(img_type);

    // write cmap spec.
//    fwrite( &zeroes, 5, 1, tga );
    tga.writeRawData((char *)zeroes,5);

    // write image spec.
//    fwrite( &xorigin, 2, 1, tga );
    tga<<(xorigin);
    tga<<( yorigin );
    tga<<( shortwidth);
    tga<<( shortheight);
    tga<<( pixdepth);
    tga<<( img_desc );


    // write image id.
    tga.writeRawData( id, idlen);




    // initial color values -- just to shut up the compiler.
    nc = 0;

    // color correction -- data is in RGB, need BGR.
    // also run-length-encoding.
    for( i = 0; i < size; i++ ) {

        idx = i * format;

        row = i / width;
        column = i % width;

        //printf( "row: %d, col: %d\n", row, column );
        pixbuf = 0;
        for( j = 0; j < format; j++ ) {
            pixbuf += dat[idx+j] << (8 * j);
        }

        switch( format ) {

        case TGA_TRUECOLOR_24:

            pixbuf = ((pixbuf & 0xFF) << 16) +
                     (pixbuf & 0xFF00) +
                     ((pixbuf & 0xFF0000) >> 16);

            break;

        case TGA_TRUECOLOR_32:

            // need to un-premultiply alpha..

            red     = (pixbuf & 0xFF) / 255.0f;
            green   = ((pixbuf & 0xFF00) >> 8) / 255.0f;
            blue    = ((pixbuf & 0xFF0000) >> 16) / 255.0f;
            alpha   = ((pixbuf & 0xFF000000) >> 24) / 255.0f;

            if( alpha > 0.0001 ) {
                red /= alpha;
                green /= alpha;
                blue /= alpha;
            }

            // clamp to 1.0f

            red = red > 1.0f ? 255.0f : red * 255.0f;
            green = green > 1.0f ? 255.0f : green * 255.0f;
            blue = blue > 1.0f ? 255.0f : blue * 255.0f;
            alpha = alpha > 1.0f ? 255.0f : alpha * 255.0f;

            pixbuf = (quint8)blue + (((quint8)green) << 8) +
                (((quint8)red) << 16) + (((quint8)alpha) << 24);

            break;

        }


        oc = nc;

        nc = pixbuf;


        switch( state ) {

        case INIT:
            // this is just used to make sure we have 2 pixel values to consider.
            state = NONE;
            break;


        case NONE:

            if( column == 0 ) {
                // write a 1 pixel raw packet for the old pixel, then go thru again.
                repcount = 0;
                tga<<( repcount);

                memcpy(numberOc, &oc, format);
                tga.writeRawData(  numberOc, format);

                state = NONE;
                break;
            }

            if( nc == oc ) {
                repcount = 0;
                state = RLP;
            } else {
                repcount = 0;
                state = RAWP;
                for( j = 0; j < format; j++ ) {

                    rawbuf[(repcount * format) + j] = *(((quint8 *)(&oc)) + j);

                }
            }
            break;


        case RLP:
            repcount++;

            if( column == 0 ) {
                // finish off rlp.
                repcount |= 0x80;
                tga<<( repcount);

                memcpy(numberOc, &oc, format);
                tga.writeRawData(  numberOc, format);

                state = NONE;
                break;
            }

            if( repcount == 127 ) {
                // finish off rlp.
                repcount |= 0x80;
                tga<<( repcount);

                memcpy(numberOc, &oc, format);
                tga.writeRawData(  numberOc, format);

                state = NONE;
                break;
            }

            if( nc != oc ) {
                // finish off rlp
                repcount |= 0x80;
                tga<<( repcount);

                memcpy(numberOc, &oc, format);
                tga.writeRawData(  numberOc, format);

                state = NONE;
            }
            break;


        case RAWP:
            repcount++;

            if( column == 0 ) {
                // finish off rawp.
                for( j = 0; j < format; j++ ) {

                    rawbuf[(repcount * format) + j] = *(((quint8 *)(&oc)) + j);

                }
                tga<<( repcount);
                tga.writeRawData( (char *) rawbuf, (repcount + 1) * format);
                state = NONE;
                break;
            }

            if( repcount == 127 ) {
                // finish off rawp.
                for( j = 0; j < format; j++ ) {

                    rawbuf[(repcount * format) + j] = *(((quint8 *)(&oc)) + j);

                }
                tga<<( repcount);
                tga.writeRawData( (char *) rawbuf, (repcount + 1) * format);
                state = NONE;
                break;
            }

            if( nc == oc ) {
                // finish off rawp
                repcount--;
                tga<<( repcount);
                tga.writeRawData( (char *) rawbuf, (repcount + 1) * format);

                // start new rlp
                repcount = 0;
                state = RLP;
                break;
            }

            // continue making rawp
            for( j = 0; j < format; j++ ) {

                rawbuf[(repcount * format) + j] = *(((quint8 *)(&oc)) + j);

            }

            break;

        }


    }


    // clean up state.


    switch( state ) {

    case INIT:
        break;

    case NONE:{
        // write the last 2 pixels in a raw packet.
        tga<<( one);

        memcpy(numberOc, &oc, format);
        tga.writeRawData(  numberOc, format);

        memcpy(numberNc, &nc, format);
        tga.writeRawData(  numberNc, format);


        break;
    }

    case RLP:{
        repcount++;
        repcount |= 0x80;
        tga<<( repcount);

        memcpy(numberOc, &oc, format);
        tga.writeRawData(  numberOc, format);

        break;
    }

    case RAWP:{
        repcount++;
        for( j = 0; j < format; j++ ) {

            rawbuf[(repcount * format) + j] = *(((quint8 *)(&oc)) + j);

        }
        tga<<( repcount );
        tga.writeRawData( (char *) rawbuf, (repcount + 1) * 3);
        break;
    }

    }

    free( rawbuf );

    return( true );
}


bool TGAHandler::canRead(QIODevice *device)
{
    if (!device) {
        qWarning("TGAHandler::canRead() called with no device");
        return false;
    }

    qint64 oldPos = device->pos();
    QByteArray head = device->read(TgaHeader::SIZE);
    int readBytes = head.size();

    if (device->isSequential()) {
        for (int pos = readBytes - 1; pos >= 0; --pos) {
            device->ungetChar(head[pos]);
        }
    } else {
        device->seek(oldPos);
    }

    if (readBytes < TgaHeader::SIZE) {
        return false;
    }

    QDataStream stream(head);
    stream.setByteOrder(QDataStream::LittleEndian);
    TgaHeader tga;
    stream >> tga;
    return IsSupported(tga);
}

QImageIOPlugin::Capabilities TGAPlugin::capabilities(QIODevice *device, const QByteArray &format) const
{
    if (format == "tga") {
        return Capabilities(CanRead | CanWrite);
    }
    if (!format.isEmpty()) {
        return {};
    }
    if (!device->isOpen()) {
        return {};
    }

    Capabilities cap;
    if (device->isReadable() && TGAHandler::canRead(device)) {
        cap |= CanRead;
    }
    if (device->isWritable()) {
        cap |= CanWrite;
    }
    return cap;
}

QImageIOHandler *TGAPlugin::create(QIODevice *device, const QByteArray &format) const
{
    QImageIOHandler *handler = new TGAHandler;
    handler->setDevice(device);
    handler->setFormat(format);
    return handler;
}



