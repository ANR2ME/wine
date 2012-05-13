/*
 * Copyright (C) 2009-2010 Tony Wasserka
 * Copyright (C) 2012 Józef Kucia
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#include "wine/debug.h"
#include "wine/unicode.h"
#include "d3dx9_36_private.h"

#include "initguid.h"
#include "wincodec.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3dx);


/* Wine-specific WIC GUIDs */
DEFINE_GUID(GUID_WineContainerFormatTga, 0x0c44fda1,0xa5c5,0x4298,0x96,0x85,0x47,0x3f,0xc1,0x7c,0xd3,0x22);

static const struct
{
    const GUID *wic_guid;
    D3DFORMAT d3dformat;
} wic_pixel_formats[] = {
    { &GUID_WICPixelFormat8bppIndexed, D3DFMT_L8 },
    { &GUID_WICPixelFormat1bppIndexed, D3DFMT_L8 },
    { &GUID_WICPixelFormat4bppIndexed, D3DFMT_L8 },
    { &GUID_WICPixelFormat16bppBGR555, D3DFMT_X1R5G5B5 },
    { &GUID_WICPixelFormat16bppBGR565, D3DFMT_R5G6B5 },
    { &GUID_WICPixelFormat24bppBGR, D3DFMT_R8G8B8 },
    { &GUID_WICPixelFormat32bppBGR, D3DFMT_X8R8G8B8 },
    { &GUID_WICPixelFormat32bppBGRA, D3DFMT_A8R8G8B8 }
};

static D3DFORMAT wic_guid_to_d3dformat(const GUID *guid)
{
    int i;

    for (i = 0; i < sizeof(wic_pixel_formats) / sizeof(wic_pixel_formats[0]); i++)
    {
        if (IsEqualGUID(wic_pixel_formats[i].wic_guid, guid))
            return wic_pixel_formats[i].d3dformat;
    }

    return D3DFMT_UNKNOWN;
}

static const GUID *d3dformat_to_wic_guid(D3DFORMAT format)
{
    int i;

    for (i = 0; i < sizeof(wic_pixel_formats) / sizeof(wic_pixel_formats[0]); i++)
    {
        if (wic_pixel_formats[i].d3dformat == format)
            return wic_pixel_formats[i].wic_guid;
    }

    return NULL;
}

/* dds_header.flags */
#define DDS_CAPS 0x1
#define DDS_HEIGHT 0x2
#define DDS_WIDTH 0x2
#define DDS_PITCH 0x8
#define DDS_PIXELFORMAT 0x1000
#define DDS_MIPMAPCOUNT 0x20000
#define DDS_LINEARSIZE 0x80000
#define DDS_DEPTH 0x800000

/* dds_header.caps */
#define DDS_CAPS_COMPLEX 0x8
#define DDS_CAPS_TEXTURE 0x1000
#define DDS_CAPS_MIPMAP 0x400000

/* dds_header.caps2 */
#define DDS_CAPS2_CUBEMAP 0x200
#define DDS_CAPS2_CUBEMAP_POSITIVEX 0x400
#define DDS_CAPS2_CUBEMAP_NEGATIVEX 0x800
#define DDS_CAPS2_CUBEMAP_POSITIVEY 0x1000
#define DDS_CAPS2_CUBEMAP_NEGATIVEY 0x2000
#define DDS_CAPS2_CUBEMAP_POSITIVEZ 0x4000
#define DDS_CAPS2_CUBEMAP_NEGATIVEZ 0x8000
#define DDS_CAPS2_CUBEMAP_ALL_FACES ( DDS_CAPS2_CUBEMAP_POSITIVEX | DDS_CAPS2_CUBEMAP_NEGATIVEX \
                                    | DDS_CAPS2_CUBEMAP_POSITIVEY | DDS_CAPS2_CUBEMAP_NEGATIVEY \
                                    | DDS_CAPS2_CUBEMAP_POSITIVEZ | DDS_CAPS2_CUBEMAP_NEGATIVEZ )
#define DDS_CAPS2_VOLUME 0x200000

/* dds_pixel_format.flags */
#define DDS_PF_ALPHA 0x1
#define DDS_PF_ALPHA_ONLY 0x2
#define DDS_PF_FOURCC 0x4
#define DDS_PF_RGB 0x40
#define DDS_PF_YUV 0x200
#define DDS_PF_LUMINANCE 0x20000
#define DDS_PF_BUMPDUDV 0x80000

struct dds_pixel_format
{
    DWORD size;
    DWORD flags;
    DWORD fourcc;
    DWORD bpp;
    DWORD rmask;
    DWORD gmask;
    DWORD bmask;
    DWORD amask;
};

struct dds_header
{
    DWORD signature;
    DWORD size;
    DWORD flags;
    DWORD height;
    DWORD width;
    DWORD pitch_or_linear_size;
    DWORD depth;
    DWORD miplevels;
    DWORD reserved[11];
    struct dds_pixel_format pixel_format;
    DWORD caps;
    DWORD caps2;
    DWORD caps3;
    DWORD caps4;
    DWORD reserved2;
};

static D3DFORMAT dds_fourcc_to_d3dformat(DWORD fourcc)
{
    int i;
    static const DWORD known_fourcc[] = {
        MAKEFOURCC('U','Y','V','Y'),
        MAKEFOURCC('Y','U','Y','2'),
        MAKEFOURCC('R','G','B','G'),
        MAKEFOURCC('G','R','G','B'),
        MAKEFOURCC('D','X','T','1'),
        MAKEFOURCC('D','X','T','2'),
        MAKEFOURCC('D','X','T','3'),
        MAKEFOURCC('D','X','T','4'),
        MAKEFOURCC('D','X','T','5')
    };

    for (i = 0; i < sizeof(known_fourcc) / sizeof(known_fourcc[0]); i++)
    {
        if (known_fourcc[i] == fourcc)
            return fourcc;
    }

    WARN("Unknown FourCC %#x\n", fourcc);
    return D3DFMT_UNKNOWN;
}

static D3DFORMAT dds_rgb_to_d3dformat(const struct dds_pixel_format *pixel_format)
{
    int i;
    static const struct {
        DWORD bpp;
        DWORD rmask;
        DWORD gmask;
        DWORD bmask;
        DWORD amask;
        D3DFORMAT format;
    } rgb_pixel_formats[] = {
        { 8, 0xe0, 0x1c, 0x03, 0, D3DFMT_R3G3B2 },
        { 16, 0xf800, 0x07e0, 0x001f, 0x0000, D3DFMT_R5G6B5 },
        { 16, 0x7c00, 0x03e0, 0x001f, 0x8000, D3DFMT_A1R5G5B5 },
        { 16, 0x7c00, 0x03e0, 0x001f, 0x0000, D3DFMT_X1R5G5B5 },
        { 16, 0x0f00, 0x00f0, 0x000f, 0xf000, D3DFMT_A4R4G4B4 },
        { 16, 0x0f00, 0x00f0, 0x000f, 0x0000, D3DFMT_X4R4G4B4 },
        { 16, 0x00e0, 0x001c, 0x0003, 0xff00, D3DFMT_A8R3G3B2 },
        { 24, 0xff0000, 0x00ff00, 0x0000ff, 0x000000, D3DFMT_R8G8B8 },
        { 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000, D3DFMT_A8R8G8B8 },
        { 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000, D3DFMT_X8R8G8B8 },
        { 32, 0x3ff00000, 0x000ffc00, 0x000003ff, 0xc0000000, D3DFMT_A2B10G10R10 },
        { 32, 0x000003ff, 0x000ffc00, 0x3ff00000, 0xc0000000, D3DFMT_A2R10G10B10 },
        { 32, 0x0000ffff, 0xffff0000, 0x00000000, 0x00000000, D3DFMT_G16R16 },
    };

    for (i = 0; i < sizeof(rgb_pixel_formats) / sizeof(rgb_pixel_formats[0]); i++)
    {
        if (rgb_pixel_formats[i].bpp == pixel_format->bpp
            && rgb_pixel_formats[i].rmask == pixel_format->rmask
            && rgb_pixel_formats[i].gmask == pixel_format->gmask
            && rgb_pixel_formats[i].bmask == pixel_format->bmask)
        {
            if ((pixel_format->flags & DDS_PF_ALPHA) && rgb_pixel_formats[i].amask == pixel_format->amask)
                return rgb_pixel_formats[i].format;
            if (rgb_pixel_formats[i].amask == 0)
                return rgb_pixel_formats[i].format;
        }
    }

    WARN("Unknown RGB pixel format (%#x, %#x, %#x, %#x)\n",
        pixel_format->rmask, pixel_format->gmask, pixel_format->bmask, pixel_format->amask);
    return D3DFMT_UNKNOWN;
}

static D3DFORMAT dds_luminance_to_d3dformat(const struct dds_pixel_format *pixel_format)
{
    if (pixel_format->bpp == 8)
    {
        if (pixel_format->rmask == 0xff)
            return D3DFMT_L8;
        if ((pixel_format->flags & DDS_PF_ALPHA) && pixel_format->rmask == 0x0f && pixel_format->amask == 0xf0)
            return D3DFMT_A4L4;
    }
    if (pixel_format->bpp == 16)
    {
        if (pixel_format->rmask == 0xffff)
            return D3DFMT_L16;
        if ((pixel_format->flags & DDS_PF_ALPHA) && pixel_format->rmask == 0x00ff && pixel_format->amask == 0xff00)
            return D3DFMT_A8L8;
    }

    WARN("Unknown luminance pixel format (bpp %#x, l %#x, a %#x)\n",
        pixel_format->bpp, pixel_format->rmask, pixel_format->amask);
    return D3DFMT_UNKNOWN;
}

static D3DFORMAT dds_alpha_to_d3dformat(const struct dds_pixel_format *pixel_format)
{
    if (pixel_format->bpp == 8 && pixel_format->amask == 0xff)
        return D3DFMT_A8;

    WARN("Unknown Alpha pixel format (%#x, %#x)\n", pixel_format->bpp, pixel_format->rmask);
    return D3DFMT_UNKNOWN;
}

static D3DFORMAT dds_bump_to_d3dformat(const struct dds_pixel_format *pixel_format)
{
    if (pixel_format->bpp == 16 && pixel_format->rmask == 0x00ff && pixel_format->gmask == 0xff00)
        return D3DFMT_V8U8;
    if (pixel_format->bpp == 32 && pixel_format->rmask == 0x0000ffff && pixel_format->gmask == 0xffff0000)
        return D3DFMT_V16U16;

    WARN("Unknown bump pixel format (%#x, %#x, %#x, %#x, %#x)\n", pixel_format->bpp,
        pixel_format->rmask, pixel_format->gmask, pixel_format->bmask, pixel_format->amask);
    return D3DFMT_UNKNOWN;
}

static D3DFORMAT dds_pixel_format_to_d3dformat(const struct dds_pixel_format *pixel_format)
{
    if (pixel_format->flags & DDS_PF_FOURCC)
        return dds_fourcc_to_d3dformat(pixel_format->fourcc);
    if (pixel_format->flags & DDS_PF_RGB)
        return dds_rgb_to_d3dformat(pixel_format);
    if (pixel_format->flags & DDS_PF_LUMINANCE)
        return dds_luminance_to_d3dformat(pixel_format);
    if (pixel_format->flags & DDS_PF_ALPHA_ONLY)
        return dds_alpha_to_d3dformat(pixel_format);
    if (pixel_format->flags & DDS_PF_BUMPDUDV)
        return dds_bump_to_d3dformat(pixel_format);

    WARN("Unknown pixel format (flags %#x, fourcc %#x, bpp %#x, r %#x, g %#x, b %#x, a %#x)\n",
        pixel_format->flags, pixel_format->fourcc, pixel_format->bpp,
        pixel_format->rmask, pixel_format->gmask, pixel_format->bmask, pixel_format->amask);
    return D3DFMT_UNKNOWN;
}

static HRESULT calculate_dds_surface_size(const D3DXIMAGE_INFO *img_info,
    UINT width, UINT height, UINT *pitch, UINT *size)
{
    const PixelFormatDesc *format_desc = get_format_info(img_info->Format);
    if (format_desc->format == D3DFMT_UNKNOWN)
        return E_NOTIMPL;

    if (format_desc->block_width != 1 || format_desc->block_height != 1)
    {
        *pitch = format_desc->block_byte_count
            * max(1, (width + format_desc->block_width - 1) / format_desc->block_width);
        *size = *pitch
            * max(1, (height + format_desc->block_height - 1) / format_desc->block_height);
    }
    else
    {
        *pitch = width * format_desc->bytes_per_pixel;
        *size = *pitch * height;
    }

    return D3D_OK;
}

/************************************************************
* get_image_info_from_dds
*
* Fills a D3DXIMAGE_INFO structure with information
* about a DDS file stored in the memory.
*
* PARAMS
*   buffer  [I] pointer to DDS data
*   length  [I] size of DDS data
*   info    [O] pointer to D3DXIMAGE_INFO structure
*
* RETURNS
*   Success: D3D_OK
*   Failure: D3DXERR_INVALIDDATA
*
*/
static HRESULT get_image_info_from_dds(const void *buffer, UINT length, D3DXIMAGE_INFO *info)
{
   UINT i;
   UINT faces = 0;
   UINT width, height;
   const struct dds_header *header = buffer;
   UINT expected_length = 0;

   if (length < sizeof(*header) || !info)
       return D3DXERR_INVALIDDATA;

   if (header->pixel_format.size != sizeof(header->pixel_format))
       return D3DXERR_INVALIDDATA;

   info->Width = header->width;
   info->Height = header->height;
   info->Depth = 1;
   info->MipLevels = (header->flags & DDS_MIPMAPCOUNT) ?  header->miplevels : 1;

   info->Format = dds_pixel_format_to_d3dformat(&header->pixel_format);
   if (info->Format == D3DFMT_UNKNOWN)
       return D3DXERR_INVALIDDATA;

   TRACE("Pixel format is %#x\n", info->Format);

   if (header->caps2 & DDS_CAPS2_VOLUME)
   {
       info->Depth = header->depth;
       info->ResourceType = D3DRTYPE_VOLUMETEXTURE;
   }
   else if (header->caps2 & DDS_CAPS2_CUBEMAP)
   {
       DWORD face;
       for (face = DDS_CAPS2_CUBEMAP_POSITIVEX; face <= DDS_CAPS2_CUBEMAP_NEGATIVEZ; face <<= 1)
       {
           if (header->caps2 & face)
               faces++;
       }
       info->ResourceType = D3DRTYPE_CUBETEXTURE;
   }
   else
   {
       faces = 1;
       info->ResourceType = D3DRTYPE_TEXTURE;
   }

   /* calculate the expected length */
   width = info->Width;
   height = info->Height;
   for (i = 0; i < info->MipLevels; i++)
   {
       UINT pitch, size = 0;
       calculate_dds_surface_size(info, width, height, &pitch, &size);
       expected_length += size;
       width = max(1, width / 2);
       height = max(1, width / 2);
   }

   expected_length *= faces;
   expected_length += sizeof(*header);
   if (length < expected_length)
       return D3DXERR_INVALIDDATA;

   info->ImageFileFormat = D3DXIFF_DDS;

   return D3D_OK;
}

static HRESULT load_surface_from_dds(IDirect3DSurface9 *dst_surface, const PALETTEENTRY *dst_palette,
    const RECT *dst_rect, const void *src_data, const RECT *src_rect, DWORD filter, D3DCOLOR color_key,
    const D3DXIMAGE_INFO *src_info)
{
    UINT size;
    UINT src_pitch;
    const struct dds_header *header = src_data;
    const BYTE *pixels = (BYTE *)(header + 1);

    if (src_info->ResourceType != D3DRTYPE_TEXTURE)
        return D3DXERR_INVALIDDATA;

    if (FAILED(calculate_dds_surface_size(src_info, src_info->Width, src_info->Height, &src_pitch, &size)))
        return E_NOTIMPL;

    return D3DXLoadSurfaceFromMemory(dst_surface, dst_palette, dst_rect, pixels, src_info->Format,
        src_pitch, NULL, src_rect, filter, color_key);
}

HRESULT load_texture_from_dds(IDirect3DTexture9 *texture, const void *src_data, const PALETTEENTRY *palette,
    DWORD filter, D3DCOLOR color_key, const D3DXIMAGE_INFO *src_info)

{
    HRESULT hr;
    RECT src_rect;
    UINT src_pitch;
    UINT mip_level;
    UINT mip_levels;
    UINT mip_level_size;
    UINT width, height;
    IDirect3DSurface9 *surface;
    const struct dds_header *header = src_data;
    const BYTE *pixels = (BYTE *)(header + 1);

    if (src_info->ResourceType != D3DRTYPE_TEXTURE)
        return D3DXERR_INVALIDDATA;

    width = src_info->Width;
    height = src_info->Height;
    mip_levels = min(src_info->MipLevels, IDirect3DTexture9_GetLevelCount(texture));
    for (mip_level = 0; mip_level < mip_levels; mip_level++)
    {
        hr = calculate_dds_surface_size(src_info, width, height, &src_pitch, &mip_level_size);
        if (FAILED(hr)) return hr;

        SetRect(&src_rect, 0, 0, width, height);

        IDirect3DTexture9_GetSurfaceLevel(texture, mip_level, &surface);
        hr = D3DXLoadSurfaceFromMemory(surface, palette, NULL, pixels, src_info->Format, src_pitch,
            NULL, &src_rect, filter, color_key);
        IDirect3DSurface9_Release(surface);
        if (FAILED(hr)) return hr;

        pixels += mip_level_size;
        width = max(1, width / 2);
        height = max(1, height / 2);
    }

    return D3D_OK;
}

HRESULT load_cube_texture_from_dds(IDirect3DCubeTexture9 *cube_texture, const void *src_data,
    const PALETTEENTRY *palette, DWORD filter, DWORD color_key, const D3DXIMAGE_INFO *src_info)
{
    HRESULT hr;
    int face;
    int mip_level;
    UINT size;
    RECT src_rect;
    UINT src_pitch;
    UINT mip_levels;
    UINT mip_level_size;
    IDirect3DSurface9 *surface;
    const struct dds_header *header = src_data;
    const BYTE *pixels = (BYTE *)(header + 1);

    if (src_info->ResourceType != D3DRTYPE_CUBETEXTURE)
        return D3DXERR_INVALIDDATA;

    if ((header->caps2 & DDS_CAPS2_CUBEMAP_ALL_FACES) != DDS_CAPS2_CUBEMAP_ALL_FACES)
    {
        WARN("Only full cubemaps are supported\n");
        return D3DXERR_INVALIDDATA;
    }

    mip_levels = min(src_info->MipLevels, IDirect3DCubeTexture9_GetLevelCount(cube_texture));
    for (face = D3DCUBEMAP_FACE_POSITIVE_X; face <= D3DCUBEMAP_FACE_NEGATIVE_Z; face++)
    {
        size = src_info->Width;
        for (mip_level = 0; mip_level < src_info->MipLevels; mip_level++)
        {
            hr = calculate_dds_surface_size(src_info, size, size, &src_pitch, &mip_level_size);
            if (FAILED(hr)) return hr;

            /* if texture has fewer mip levels than DDS file, skip excessive mip levels */
            if (mip_level < mip_levels)
            {
                SetRect(&src_rect, 0, 0, size, size);

                IDirect3DCubeTexture9_GetCubeMapSurface(cube_texture, face, mip_level, &surface);
                hr = D3DXLoadSurfaceFromMemory(surface, palette, NULL, pixels, src_info->Format, src_pitch,
                    NULL, &src_rect, filter, color_key);
                IDirect3DSurface9_Release(surface);
                if (FAILED(hr)) return hr;
            }

            pixels += mip_level_size;
            size = max(1, size / 2);
        }
    }

    return D3D_OK;
}

/************************************************************
 * D3DXGetImageInfoFromFileInMemory
 *
 * Fills a D3DXIMAGE_INFO structure with info about an image
 *
 * PARAMS
 *   data     [I] pointer to the image file data
 *   datasize [I] size of the passed data
 *   info     [O] pointer to the destination structure
 *
 * RETURNS
 *   Success: D3D_OK, if info is not NULL and data and datasize make up a valid image file or
 *                    if info is NULL and data and datasize are not NULL
 *   Failure: D3DXERR_INVALIDDATA, if data is no valid image file and datasize and info are not NULL
 *            D3DERR_INVALIDCALL, if data is NULL or
 *                                if datasize is 0
 *
 * NOTES
 *   datasize may be bigger than the actual file size
 *
 */
HRESULT WINAPI D3DXGetImageInfoFromFileInMemory(LPCVOID data, UINT datasize, D3DXIMAGE_INFO *info)
{
    IWICImagingFactory *factory;
    IWICBitmapDecoder *decoder = NULL;
    IWICStream *stream;
    HRESULT hr;
    HRESULT initresult;

    TRACE("(%p, %d, %p)\n", data, datasize, info);

    if (!data || !datasize)
        return D3DERR_INVALIDCALL;

    if (!info)
        return D3D_OK;

    if ((datasize >= 4) && !strncmp(data, "DDS ", 4)) {
        TRACE("File type is DDS\n");
        return get_image_info_from_dds(data, datasize, info);
    }

    initresult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void**)&factory);

    if (SUCCEEDED(hr)) {
        IWICImagingFactory_CreateStream(factory, &stream);
        IWICStream_InitializeFromMemory(stream, (BYTE*)data, datasize);
        hr = IWICImagingFactory_CreateDecoderFromStream(factory, (IStream*)stream, NULL, 0, &decoder);
        IStream_Release(stream);
        IWICImagingFactory_Release(factory);
    }

    if (FAILED(hr)) {
        if ((datasize >= 2) && (!strncmp(data, "P3", 2) || !strncmp(data, "P6", 2)))
            FIXME("File type PPM is not supported yet\n");
        else if ((datasize >= 2) && !strncmp(data, "BM", 2))
            FIXME("File type DIB is not supported yet\n");
        else if ((datasize >= 10) && !strncmp(data, "#?RADIANCE", 10))
            FIXME("File type HDR is not supported yet\n");
        else if ((datasize >= 2) && (!strncmp(data, "PF", 2) || !strncmp(data, "Pf", 2)))
            FIXME("File type PFM is not supported yet\n");
    }

    if (SUCCEEDED(hr)) {
        GUID container_format;
        UINT frame_count;

        hr = IWICBitmapDecoder_GetContainerFormat(decoder, &container_format);
        if (SUCCEEDED(hr)) {
            if (IsEqualGUID(&container_format, &GUID_ContainerFormatBmp)) {
                TRACE("File type is BMP\n");
                info->ImageFileFormat = D3DXIFF_BMP;
            } else if (IsEqualGUID(&container_format, &GUID_ContainerFormatPng)) {
                TRACE("File type is PNG\n");
                info->ImageFileFormat = D3DXIFF_PNG;
            } else if(IsEqualGUID(&container_format, &GUID_ContainerFormatJpeg)) {
                TRACE("File type is JPG\n");
                info->ImageFileFormat = D3DXIFF_JPG;
            } else if(IsEqualGUID(&container_format, &GUID_WineContainerFormatTga)) {
                TRACE("File type is TGA\n");
                info->ImageFileFormat = D3DXIFF_TGA;
            } else {
                WARN("Unsupported image file format %s\n", debugstr_guid(&container_format));
                hr = D3DXERR_INVALIDDATA;
            }
        }

        if (SUCCEEDED(hr))
            hr = IWICBitmapDecoder_GetFrameCount(decoder, &frame_count);
        if (SUCCEEDED(hr) && !frame_count)
            hr = D3DXERR_INVALIDDATA;

        if (SUCCEEDED(hr)) {
            IWICBitmapFrameDecode *frame = NULL;

            hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);

            if (SUCCEEDED(hr))
                hr = IWICBitmapFrameDecode_GetSize(frame, &info->Width, &info->Height);

            if (SUCCEEDED(hr)) {
                WICPixelFormatGUID pixel_format;

                hr = IWICBitmapFrameDecode_GetPixelFormat(frame, &pixel_format);
                if (SUCCEEDED(hr)) {
                    info->Format = wic_guid_to_d3dformat(&pixel_format);
                    if (info->Format == D3DFMT_UNKNOWN) {
                        WARN("Unsupported pixel format %s\n", debugstr_guid(&pixel_format));
                        hr = D3DXERR_INVALIDDATA;
                    }
                }
            }

            if (frame)
                 IWICBitmapFrameDecode_Release(frame);

            info->Depth = 1;
            info->MipLevels = 1;
            info->ResourceType = D3DRTYPE_TEXTURE;
        }
    }

    if (decoder)
        IWICBitmapDecoder_Release(decoder);

    if (SUCCEEDED(initresult))
        CoUninitialize();

    if (FAILED(hr)) {
        TRACE("Invalid or unsupported image file\n");
        return D3DXERR_INVALIDDATA;
    }

    return D3D_OK;
}

/************************************************************
 * D3DXGetImageInfoFromFile
 *
 * RETURNS
 *   Success: D3D_OK, if we successfully load a valid image file or
 *                    if we successfully load a file which is no valid image and info is NULL
 *   Failure: D3DXERR_INVALIDDATA, if we fail to load file or
 *                                 if file is not a valid image file and info is not NULL
 *            D3DERR_INVALIDCALL, if file is NULL
 *
 */
HRESULT WINAPI D3DXGetImageInfoFromFileA(LPCSTR file, D3DXIMAGE_INFO *info)
{
    LPWSTR widename;
    HRESULT hr;
    int strlength;

    TRACE("(%s, %p): relay\n", debugstr_a(file), info);

    if( !file ) return D3DERR_INVALIDCALL;

    strlength = MultiByteToWideChar(CP_ACP, 0, file, -1, NULL, 0);
    widename = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, strlength * sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, file, -1, widename, strlength);

    hr = D3DXGetImageInfoFromFileW(widename, info);
    HeapFree(GetProcessHeap(), 0, widename);

    return hr;
}

HRESULT WINAPI D3DXGetImageInfoFromFileW(LPCWSTR file, D3DXIMAGE_INFO *info)
{
    HRESULT hr;
    DWORD size;
    LPVOID buffer;

    TRACE("(%s, %p): relay\n", debugstr_w(file), info);

    if( !file ) return D3DERR_INVALIDCALL;

    hr = map_view_of_file(file, &buffer, &size);
    if(FAILED(hr)) return D3DXERR_INVALIDDATA;

    hr = D3DXGetImageInfoFromFileInMemory(buffer, size, info);
    UnmapViewOfFile(buffer);

    return hr;
}

/************************************************************
 * D3DXGetImageInfoFromResource
 *
 * RETURNS
 *   Success: D3D_OK, if resource is a valid image file
 *   Failure: D3DXERR_INVALIDDATA, if resource is no valid image file or NULL or
 *                                 if we fail to load resource
 *
 */
HRESULT WINAPI D3DXGetImageInfoFromResourceA(HMODULE module, LPCSTR resource, D3DXIMAGE_INFO *info)
{
    HRSRC resinfo;

    TRACE("(%p, %s, %p)\n", module, debugstr_a(resource), info);

    resinfo = FindResourceA(module, resource, (LPCSTR)RT_RCDATA);
    if(resinfo) {
        LPVOID buffer;
        HRESULT hr;
        DWORD size;

        hr = load_resource_into_memory(module, resinfo, &buffer, &size);
        if(FAILED(hr)) return D3DXERR_INVALIDDATA;
        return D3DXGetImageInfoFromFileInMemory(buffer, size, info);
    }

    resinfo = FindResourceA(module, resource, (LPCSTR)RT_BITMAP);
    if(resinfo) {
        FIXME("Implement loading bitmaps from resource type RT_BITMAP\n");
        return E_NOTIMPL;
    }
    return D3DXERR_INVALIDDATA;
}

HRESULT WINAPI D3DXGetImageInfoFromResourceW(HMODULE module, LPCWSTR resource, D3DXIMAGE_INFO *info)
{
    HRSRC resinfo;

    TRACE("(%p, %s, %p)\n", module, debugstr_w(resource), info);

    resinfo = FindResourceW(module, resource, (LPCWSTR)RT_RCDATA);
    if(resinfo) {
        LPVOID buffer;
        HRESULT hr;
        DWORD size;

        hr = load_resource_into_memory(module, resinfo, &buffer, &size);
        if(FAILED(hr)) return D3DXERR_INVALIDDATA;
        return D3DXGetImageInfoFromFileInMemory(buffer, size, info);
    }

    resinfo = FindResourceW(module, resource, (LPCWSTR)RT_BITMAP);
    if(resinfo) {
        FIXME("Implement loading bitmaps from resource type RT_BITMAP\n");
        return E_NOTIMPL;
    }
    return D3DXERR_INVALIDDATA;
}

/************************************************************
 * D3DXLoadSurfaceFromFileInMemory
 *
 * Loads data from a given buffer into a surface and fills a given
 * D3DXIMAGE_INFO structure with info about the source data.
 *
 * PARAMS
 *   pDestSurface [I] pointer to the surface
 *   pDestPalette [I] palette to use
 *   pDestRect    [I] to be filled area of the surface
 *   pSrcData     [I] pointer to the source data
 *   SrcDataSize  [I] size of the source data in bytes
 *   pSrcRect     [I] area of the source data to load
 *   dwFilter     [I] filter to apply on stretching
 *   Colorkey     [I] colorkey
 *   pSrcInfo     [O] pointer to a D3DXIMAGE_INFO structure
 *
 * RETURNS
 *   Success: D3D_OK
 *   Failure: D3DERR_INVALIDCALL, if pDestSurface or pSrcData or SrcDataSize are NULL
 *            D3DXERR_INVALIDDATA, if pSrcData is no valid image file
 *
 */
HRESULT WINAPI D3DXLoadSurfaceFromFileInMemory(LPDIRECT3DSURFACE9 pDestSurface,
                                               CONST PALETTEENTRY *pDestPalette,
                                               CONST RECT *pDestRect,
                                               LPCVOID pSrcData,
                                               UINT SrcDataSize,
                                               CONST RECT *pSrcRect,
                                               DWORD dwFilter,
                                               D3DCOLOR Colorkey,
                                               D3DXIMAGE_INFO *pSrcInfo)
{
    D3DXIMAGE_INFO imginfo;
    HRESULT hr;

    IWICImagingFactory *factory;
    IWICBitmapDecoder *decoder;
    IWICBitmapFrameDecode *bitmapframe;
    IWICStream *stream;

    const PixelFormatDesc *formatdesc;
    WICRect wicrect;
    RECT rect;

    TRACE("(%p, %p, %p, %p, %d, %p, %d, %x, %p)\n", pDestSurface, pDestPalette, pDestRect, pSrcData,
        SrcDataSize, pSrcRect, dwFilter, Colorkey, pSrcInfo);

    if (!pDestSurface || !pSrcData || !SrcDataSize)
        return D3DERR_INVALIDCALL;

    hr = D3DXGetImageInfoFromFileInMemory(pSrcData, SrcDataSize, &imginfo);

    if (FAILED(hr))
        return hr;

    if (pSrcRect)
    {
        wicrect.X = pSrcRect->left;
        wicrect.Y = pSrcRect->top;
        wicrect.Width = pSrcRect->right - pSrcRect->left;
        wicrect.Height = pSrcRect->bottom - pSrcRect->top;
    }
    else
    {
        wicrect.X = 0;
        wicrect.Y = 0;
        wicrect.Width = imginfo.Width;
        wicrect.Height = imginfo.Height;
    }

    SetRect(&rect, 0, 0, wicrect.Width, wicrect.Height);

    if (imginfo.ImageFileFormat == D3DXIFF_DDS)
    {
        hr = load_surface_from_dds(pDestSurface, pDestPalette, pDestRect, pSrcData, &rect,
            dwFilter, Colorkey, &imginfo);
        if (SUCCEEDED(hr) && pSrcInfo)
            *pSrcInfo = imginfo;
        return hr;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void**)&factory)))
        goto cleanup_err;

    if (FAILED(IWICImagingFactory_CreateStream(factory, &stream)))
    {
        IWICImagingFactory_Release(factory);
        goto cleanup_err;
    }

    IWICStream_InitializeFromMemory(stream, (BYTE*)pSrcData, SrcDataSize);

    hr = IWICImagingFactory_CreateDecoderFromStream(factory, (IStream*)stream, NULL, 0, &decoder);

    IStream_Release(stream);
    IWICImagingFactory_Release(factory);

    if (FAILED(hr))
        goto cleanup_err;

    hr = IWICBitmapDecoder_GetFrame(decoder, 0, &bitmapframe);

    if (FAILED(hr))
        goto cleanup_bmp;

    formatdesc = get_format_info(imginfo.Format);

    if (formatdesc->format == D3DFMT_UNKNOWN)
    {
        FIXME("Unsupported pixel format\n");
        hr = D3DXERR_INVALIDDATA;
    }
    else
    {
        BYTE *buffer;
        DWORD pitch;

        pitch = formatdesc->bytes_per_pixel * wicrect.Width;
        buffer = HeapAlloc(GetProcessHeap(), 0, pitch * wicrect.Height);

        hr = IWICBitmapFrameDecode_CopyPixels(bitmapframe, &wicrect, pitch,
                                              pitch * wicrect.Height, buffer);

        if (SUCCEEDED(hr))
        {
            hr = D3DXLoadSurfaceFromMemory(pDestSurface, pDestPalette, pDestRect,
                                           buffer, imginfo.Format, pitch,
                                           NULL, &rect, dwFilter, Colorkey);
        }

        HeapFree(GetProcessHeap(), 0, buffer);
    }

    IWICBitmapFrameDecode_Release(bitmapframe);

cleanup_bmp:
    IWICBitmapDecoder_Release(decoder);

cleanup_err:
    CoUninitialize();

    if (FAILED(hr))
        return D3DXERR_INVALIDDATA;

    if (pSrcInfo)
        *pSrcInfo = imginfo;

    return D3D_OK;
}

/************************************************************
 * D3DXLoadSurfaceFromFile
 */
HRESULT WINAPI D3DXLoadSurfaceFromFileA(LPDIRECT3DSURFACE9 pDestSurface,
                                        CONST PALETTEENTRY *pDestPalette,
                                        CONST RECT *pDestRect,
                                        LPCSTR pSrcFile,
                                        CONST RECT *pSrcRect,
                                        DWORD dwFilter,
                                        D3DCOLOR Colorkey,
                                        D3DXIMAGE_INFO *pSrcInfo)
{
    LPWSTR pWidename;
    HRESULT hr;
    int strlength;

    TRACE("(%p, %p, %p, %s, %p, %u, %#x, %p): relay\n", pDestSurface, pDestPalette, pDestRect, debugstr_a(pSrcFile),
        pSrcRect, dwFilter, Colorkey, pSrcInfo);

    if( !pSrcFile || !pDestSurface ) return D3DERR_INVALIDCALL;

    strlength = MultiByteToWideChar(CP_ACP, 0, pSrcFile, -1, NULL, 0);
    pWidename = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, strlength * sizeof(WCHAR));
    MultiByteToWideChar(CP_ACP, 0, pSrcFile, -1, pWidename, strlength);

    hr = D3DXLoadSurfaceFromFileW(pDestSurface, pDestPalette, pDestRect, pWidename, pSrcRect, dwFilter, Colorkey, pSrcInfo);
    HeapFree(GetProcessHeap(), 0, pWidename);

    return hr;
}

HRESULT WINAPI D3DXLoadSurfaceFromFileW(LPDIRECT3DSURFACE9 pDestSurface,
                                        CONST PALETTEENTRY *pDestPalette,
                                        CONST RECT *pDestRect,
                                        LPCWSTR pSrcFile,
                                        CONST RECT *pSrcRect,
                                        DWORD Filter,
                                        D3DCOLOR Colorkey,
                                        D3DXIMAGE_INFO *pSrcInfo)
{
    HRESULT hr;
    DWORD dwSize;
    LPVOID pBuffer;

    TRACE("(%p, %p, %p, %s, %p, %u, %#x, %p): relay\n", pDestSurface, pDestPalette, pDestRect, debugstr_w(pSrcFile),
        pSrcRect, Filter, Colorkey, pSrcInfo);

    if( !pSrcFile || !pDestSurface ) return D3DERR_INVALIDCALL;

    hr = map_view_of_file(pSrcFile, &pBuffer, &dwSize);
    if(FAILED(hr)) return D3DXERR_INVALIDDATA;

    hr = D3DXLoadSurfaceFromFileInMemory(pDestSurface, pDestPalette, pDestRect, pBuffer, dwSize, pSrcRect, Filter, Colorkey, pSrcInfo);
    UnmapViewOfFile(pBuffer);

    return hr;
}

/************************************************************
 * D3DXLoadSurfaceFromResource
 */
HRESULT WINAPI D3DXLoadSurfaceFromResourceA(LPDIRECT3DSURFACE9 pDestSurface,
                                            CONST PALETTEENTRY *pDestPalette,
                                            CONST RECT *pDestRect,
                                            HMODULE hSrcModule,
                                            LPCSTR pResource,
                                            CONST RECT *pSrcRect,
                                            DWORD dwFilter,
                                            D3DCOLOR Colorkey,
                                            D3DXIMAGE_INFO *pSrcInfo)
{
    HRSRC hResInfo;

    TRACE("(%p, %p, %p, %p, %s, %p, %u, %#x, %p): relay\n", pDestSurface, pDestPalette, pDestRect, hSrcModule,
        debugstr_a(pResource), pSrcRect, dwFilter, Colorkey, pSrcInfo);

    if( !pDestSurface ) return D3DERR_INVALIDCALL;

    hResInfo = FindResourceA(hSrcModule, pResource, (LPCSTR)RT_RCDATA);
    if(hResInfo) {
        LPVOID pBuffer;
        HRESULT hr;
        DWORD dwSize;

        hr = load_resource_into_memory(hSrcModule, hResInfo, &pBuffer, &dwSize);
        if(FAILED(hr)) return D3DXERR_INVALIDDATA;
        return D3DXLoadSurfaceFromFileInMemory(pDestSurface, pDestPalette, pDestRect, pBuffer, dwSize, pSrcRect, dwFilter, Colorkey, pSrcInfo);
    }

    hResInfo = FindResourceA(hSrcModule, pResource, (LPCSTR)RT_BITMAP);
    if(hResInfo) {
        FIXME("Implement loading bitmaps from resource type RT_BITMAP\n");
        return E_NOTIMPL;
    }
    return D3DXERR_INVALIDDATA;
}

HRESULT WINAPI D3DXLoadSurfaceFromResourceW(LPDIRECT3DSURFACE9 pDestSurface,
                                            CONST PALETTEENTRY *pDestPalette,
                                            CONST RECT *pDestRect,
                                            HMODULE hSrcModule,
                                            LPCWSTR pResource,
                                            CONST RECT *pSrcRect,
                                            DWORD dwFilter,
                                            D3DCOLOR Colorkey,
                                            D3DXIMAGE_INFO *pSrcInfo)
{
    HRSRC hResInfo;

    TRACE("(%p, %p, %p, %p, %s, %p, %u, %#x, %p): relay\n", pDestSurface, pDestPalette, pDestRect, hSrcModule,
        debugstr_w(pResource), pSrcRect, dwFilter, Colorkey, pSrcInfo);

    if( !pDestSurface ) return D3DERR_INVALIDCALL;

    hResInfo = FindResourceW(hSrcModule, pResource, (LPCWSTR)RT_RCDATA);
    if(hResInfo) {
        LPVOID pBuffer;
        HRESULT hr;
        DWORD dwSize;

        hr = load_resource_into_memory(hSrcModule, hResInfo, &pBuffer, &dwSize);
        if(FAILED(hr)) return D3DXERR_INVALIDDATA;
        return D3DXLoadSurfaceFromFileInMemory(pDestSurface, pDestPalette, pDestRect, pBuffer, dwSize, pSrcRect, dwFilter, Colorkey, pSrcInfo);
    }

    hResInfo = FindResourceW(hSrcModule, pResource, (LPCWSTR)RT_BITMAP);
    if(hResInfo) {
        FIXME("Implement loading bitmaps from resource type RT_BITMAP\n");
        return E_NOTIMPL;
    }
    return D3DXERR_INVALIDDATA;
}


/************************************************************
 * helper functions for D3DXLoadSurfaceFromMemory
 */
struct argb_conversion_info
{
    CONST PixelFormatDesc *srcformat;
    CONST PixelFormatDesc *destformat;
    DWORD srcshift[4], destshift[4];
    DWORD srcmask[4], destmask[4];
    BOOL process_channel[4];
    DWORD channelmask;
};

static void init_argb_conversion_info(CONST PixelFormatDesc *srcformat, CONST PixelFormatDesc *destformat, struct argb_conversion_info *info)
{
    UINT i;
    ZeroMemory(info->process_channel, 4 * sizeof(BOOL));
    info->channelmask = 0;

    info->srcformat  =  srcformat;
    info->destformat = destformat;

    for(i = 0;i < 4;i++) {
        /* srcshift is used to extract the _relevant_ components */
        info->srcshift[i]  =  srcformat->shift[i] + max( srcformat->bits[i] - destformat->bits[i], 0);

        /* destshift is used to move the components to the correct position */
        info->destshift[i] = destformat->shift[i] + max(destformat->bits[i] -  srcformat->bits[i], 0);

        info->srcmask[i]  = ((1 <<  srcformat->bits[i]) - 1) <<  srcformat->shift[i];
        info->destmask[i] = ((1 << destformat->bits[i]) - 1) << destformat->shift[i];

        /* channelmask specifies bits which aren't used in the source format but in the destination one */
        if(destformat->bits[i]) {
            if(srcformat->bits[i]) info->process_channel[i] = TRUE;
            else info->channelmask |= info->destmask[i];
        }
    }
}

static DWORD dword_from_bytes(CONST BYTE *src, UINT bytes_per_pixel)
{
    DWORD ret = 0;
    static BOOL fixme_once;

    if(bytes_per_pixel > sizeof(DWORD)) {
        if(!fixme_once++) FIXME("Unsupported image: %u bytes per pixel\n", bytes_per_pixel);
        bytes_per_pixel = sizeof(DWORD);
    }

    memcpy(&ret, src, bytes_per_pixel);
    return ret;
}

static void dword_to_bytes(BYTE *dst, DWORD dword, UINT bytes_per_pixel)
{
    static BOOL fixme_once;

    if(bytes_per_pixel > sizeof(DWORD)) {
        if(!fixme_once++) FIXME("Unsupported image: %u bytes per pixel\n", bytes_per_pixel);
        ZeroMemory(dst, bytes_per_pixel);
        bytes_per_pixel = sizeof(DWORD);
    }

    memcpy(dst, &dword, bytes_per_pixel);
}

/************************************************************
 * get_relevant_argb_components
 *
 * Extracts the relevant components from the source color and
 * drops the less significant bits if they aren't used by the destination format.
 */
static void get_relevant_argb_components(CONST struct argb_conversion_info *info, CONST DWORD col, DWORD *out)
{
    UINT i = 0;
    for(;i < 4;i++)
        if(info->process_channel[i])
            out[i] = (col & info->srcmask[i]) >> info->srcshift[i];
}

/************************************************************
 * make_argb_color
 *
 * Recombines the output of get_relevant_argb_components and converts
 * it to the destination format.
 */
static DWORD make_argb_color(CONST struct argb_conversion_info *info, CONST DWORD *in)
{
    UINT i;
    DWORD val = 0;

    for(i = 0;i < 4;i++) {
        if(info->process_channel[i]) {
            /* necessary to make sure that e.g. an X4R4G4B4 white maps to an R8G8B8 white instead of 0xf0f0f0 */
            signed int shift;
            for(shift = info->destshift[i]; shift > info->destformat->shift[i]; shift -= info->srcformat->bits[i]) val |= in[i] << shift;
            val |= (in[i] >> (info->destformat->shift[i] - shift)) << info->destformat->shift[i];
        }
    }
    val |= info->channelmask;   /* new channels are set to their maximal value */
    return val;
}

static void format_to_vec4(const PixelFormatDesc *format, const DWORD *src, struct vec4 *dst)
{
    DWORD mask;

    if (format->bits[1])
    {
        mask = (1 << format->bits[1]) - 1;
        dst->x = (float)((*src >> format->shift[1]) & mask) / mask;
    }
    else
        dst->x = 1.0f;

    if (format->bits[2])
    {
        mask = (1 << format->bits[2]) - 1;
        dst->y = (float)((*src >> format->shift[2]) & mask) / mask;
    }
    else
        dst->y = 1.0f;

    if (format->bits[3])
    {
        mask = (1 << format->bits[3]) - 1;
        dst->z = (float)((*src >> format->shift[3]) & mask) / mask;
    }
    else
        dst->z = 1.0f;

    if (format->bits[0])
    {
        mask = (1 << format->bits[0]) - 1;
        dst->w = (float)((*src >> format->shift[0]) & mask) / mask;
    }
    else
        dst->w = 1.0f;
}

static void format_from_vec4(const PixelFormatDesc *format, const struct vec4 *src, DWORD *dst)
{
    *dst = 0;

    if (format->bits[1])
        *dst |= (DWORD)(src->x * ((1 << format->bits[1]) - 1) + 0.5f) << format->shift[1];
    if (format->bits[2])
        *dst |= (DWORD)(src->y * ((1 << format->bits[2]) - 1) + 0.5f) << format->shift[2];
    if (format->bits[3])
        *dst |= (DWORD)(src->z * ((1 << format->bits[3]) - 1) + 0.5f) << format->shift[3];
    if (format->bits[0])
        *dst |= (DWORD)(src->w * ((1 << format->bits[0]) - 1) + 0.5f) << format->shift[0];
}

/************************************************************
 * copy_simple_data
 *
 * Copies the source buffer to the destination buffer, performing
 * any necessary format conversion and color keying.
 * Pixels outsize the source rect are blacked out.
 * Works only for ARGB formats with 1 - 4 bytes per pixel.
 */
static void copy_simple_data(const BYTE *src, UINT srcpitch, SIZE src_size, const PixelFormatDesc *srcformat,
        BYTE *dest, UINT destpitch, SIZE dst_size, const PixelFormatDesc *destformat, D3DCOLOR colorkey)
{
    struct argb_conversion_info conv_info, ck_conv_info;
    const PixelFormatDesc *ck_format = NULL;
    DWORD channels[4], pixel;
    UINT minwidth, minheight;
    UINT x, y;

    ZeroMemory(channels, sizeof(channels));
    init_argb_conversion_info(srcformat, destformat, &conv_info);

    minwidth = (src_size.cx < dst_size.cx) ? src_size.cx : dst_size.cx;
    minheight = (src_size.cy < dst_size.cy) ? src_size.cy : dst_size.cy;

    if (colorkey)
    {
        /* Color keys are always represented in D3DFMT_A8R8G8B8 format. */
        ck_format = get_format_info(D3DFMT_A8R8G8B8);
        init_argb_conversion_info(srcformat, ck_format, &ck_conv_info);
    }

    for(y = 0;y < minheight;y++) {
        const BYTE *srcptr = src + y * srcpitch;
        BYTE *destptr = dest + y * destpitch;
        DWORD val;

        for(x = 0;x < minwidth;x++) {
            /* extract source color components */
            pixel = dword_from_bytes(srcptr, srcformat->bytes_per_pixel);

            if (!srcformat->to_rgba && !destformat->from_rgba)
            {
                get_relevant_argb_components(&conv_info, pixel, channels);
                val = make_argb_color(&conv_info, channels);

                if (colorkey)
                {
                    get_relevant_argb_components(&ck_conv_info, pixel, channels);
                    pixel = make_argb_color(&ck_conv_info, channels);
                    if (pixel == colorkey)
                        val &= ~conv_info.destmask[0];
                }
            }
            else
            {
                struct vec4 color, tmp;

                format_to_vec4(srcformat, &pixel, &color);
                if (srcformat->to_rgba)
                    srcformat->to_rgba(&color, &tmp);
                else
                    tmp = color;

                if (ck_format)
                {
                    format_from_vec4(ck_format, &tmp, &pixel);
                    if (pixel == colorkey)
                        tmp.w = 0.0f;
                }

                if (destformat->from_rgba)
                    destformat->from_rgba(&tmp, &color);
                else
                    color = tmp;

                format_from_vec4(destformat, &color, &val);
            }

            dword_to_bytes(destptr, val, destformat->bytes_per_pixel);
            srcptr  +=  srcformat->bytes_per_pixel;
            destptr += destformat->bytes_per_pixel;
        }

        if (src_size.cx < dst_size.cx) /* black out remaining pixels */
            memset(destptr, 0, destformat->bytes_per_pixel * (dst_size.cx - src_size.cx));
    }
    if (src_size.cy < dst_size.cy) /* black out remaining pixels */
        memset(dest + src_size.cy * destpitch, 0, destpitch * (dst_size.cy - src_size.cy));
}

/************************************************************
 * point_filter_simple_data
 *
 * Copies the source buffer to the destination buffer, performing
 * any necessary format conversion, color keying and stretching
 * using a point filter.
 * Works only for ARGB formats with 1 - 4 bytes per pixel.
 */
static void point_filter_simple_data(const BYTE *src, UINT srcpitch, SIZE src_size, const PixelFormatDesc *srcformat,
        BYTE *dest, UINT destpitch, SIZE dst_size, const PixelFormatDesc *destformat, D3DCOLOR colorkey)
{
    struct argb_conversion_info conv_info, ck_conv_info;
    const PixelFormatDesc *ck_format = NULL;
    DWORD channels[4], pixel;

    UINT x, y;

    ZeroMemory(channels, sizeof(channels));
    init_argb_conversion_info(srcformat, destformat, &conv_info);

    if (colorkey)
    {
        /* Color keys are always represented in D3DFMT_A8R8G8B8 format. */
        ck_format = get_format_info(D3DFMT_A8R8G8B8);
        init_argb_conversion_info(srcformat, ck_format, &ck_conv_info);
    }

    for (y = 0; y < dst_size.cy; ++y)
    {
        BYTE *destptr = dest + y * destpitch;
        const BYTE *bufptr = src + srcpitch * (y * src_size.cy / dst_size.cy);

        for (x = 0; x < dst_size.cx; ++x)
        {
            const BYTE *srcptr = bufptr + (x * src_size.cx / dst_size.cx) * srcformat->bytes_per_pixel;
            DWORD val;

            /* extract source color components */
            pixel = dword_from_bytes(srcptr, srcformat->bytes_per_pixel);

            if (!srcformat->to_rgba && !destformat->from_rgba)
            {
                get_relevant_argb_components(&conv_info, pixel, channels);
                val = make_argb_color(&conv_info, channels);

                if (colorkey)
                {
                    get_relevant_argb_components(&ck_conv_info, pixel, channels);
                    pixel = make_argb_color(&ck_conv_info, channels);
                    if (pixel == colorkey)
                        val &= ~conv_info.destmask[0];
                }
            }
            else
            {
                struct vec4 color, tmp;

                format_to_vec4(srcformat, &pixel, &color);
                if (srcformat->to_rgba)
                    srcformat->to_rgba(&color, &tmp);
                else
                    tmp = color;

                if (ck_format)
                {
                    format_from_vec4(ck_format, &tmp, &pixel);
                    if (pixel == colorkey)
                        tmp.w = 0.0f;
                }

                if (destformat->from_rgba)
                    destformat->from_rgba(&tmp, &color);
                else
                    color = tmp;

                format_from_vec4(destformat, &color, &val);
            }

            dword_to_bytes(destptr, val, destformat->bytes_per_pixel);
            destptr += destformat->bytes_per_pixel;
        }
    }
}

/************************************************************
 * D3DXLoadSurfaceFromMemory
 *
 * Loads data from a given memory chunk into a surface,
 * applying any of the specified filters.
 *
 * PARAMS
 *   pDestSurface [I] pointer to the surface
 *   pDestPalette [I] palette to use
 *   pDestRect    [I] to be filled area of the surface
 *   pSrcMemory   [I] pointer to the source data
 *   SrcFormat    [I] format of the source pixel data
 *   SrcPitch     [I] number of bytes in a row
 *   pSrcPalette  [I] palette used in the source image
 *   pSrcRect     [I] area of the source data to load
 *   dwFilter     [I] filter to apply on stretching
 *   Colorkey     [I] colorkey
 *
 * RETURNS
 *   Success: D3D_OK, if we successfully load the pixel data into our surface or
 *                    if pSrcMemory is NULL but the other parameters are valid
 *   Failure: D3DERR_INVALIDCALL, if pDestSurface, SrcPitch or pSrcRect are NULL or
 *                                if SrcFormat is an invalid format (other than D3DFMT_UNKNOWN) or
 *                                if DestRect is invalid
 *            D3DXERR_INVALIDDATA, if we fail to lock pDestSurface
 *            E_FAIL, if SrcFormat is D3DFMT_UNKNOWN or the dimensions of pSrcRect are invalid
 *
 * NOTES
 *   pSrcRect specifies the dimensions of the source data;
 *   negative values for pSrcRect are allowed as we're only looking at the width and height anyway.
 *
 */
HRESULT WINAPI D3DXLoadSurfaceFromMemory(IDirect3DSurface9 *dst_surface,
        const PALETTEENTRY *dst_palette, const RECT *dst_rect, const void *src_memory,
        D3DFORMAT src_format, UINT src_pitch, const PALETTEENTRY *src_palette, const RECT *src_rect,
        DWORD filter, D3DCOLOR color_key)
{
    CONST PixelFormatDesc *srcformatdesc, *destformatdesc;
    D3DSURFACE_DESC surfdesc;
    D3DLOCKED_RECT lockrect;
    SIZE src_size, dst_size;
    HRESULT hr;

    TRACE("(%p, %p, %s, %p, %#x, %u, %p, %s %#x, 0x%08x)\n",
            dst_surface, dst_palette, wine_dbgstr_rect(dst_rect), src_memory, src_format,
            src_pitch, src_palette, wine_dbgstr_rect(src_rect), filter, color_key);

    if (!dst_surface || !src_memory || !src_rect)
        return D3DERR_INVALIDCALL;
    if (src_format == D3DFMT_UNKNOWN
            || src_rect->left >= src_rect->right
            || src_rect->top >= src_rect->bottom)
        return E_FAIL;

    if (filter == D3DX_DEFAULT)
        filter = D3DX_FILTER_TRIANGLE | D3DX_FILTER_DITHER;

    IDirect3DSurface9_GetDesc(dst_surface, &surfdesc);

    src_size.cx = src_rect->right - src_rect->left;
    src_size.cy = src_rect->bottom - src_rect->top;
    if (!dst_rect)
    {
        dst_size.cx = surfdesc.Width;
        dst_size.cy = surfdesc.Height;
    }
    else
    {
        if (dst_rect->left > dst_rect->right || dst_rect->right > surfdesc.Width)
            return D3DERR_INVALIDCALL;
        if (dst_rect->top > dst_rect->bottom || dst_rect->bottom > surfdesc.Height)
            return D3DERR_INVALIDCALL;
        if (dst_rect->left < 0 || dst_rect->top < 0)
            return D3DERR_INVALIDCALL;
        dst_size.cx = dst_rect->right - dst_rect->left;
        dst_size.cy = dst_rect->bottom - dst_rect->top;
        if (!dst_size.cx || !dst_size.cy)
            return D3D_OK;
    }

    srcformatdesc = get_format_info(src_format);
    if (srcformatdesc->type == FORMAT_UNKNOWN)
        return E_NOTIMPL;

    destformatdesc = get_format_info(surfdesc.Format);
    if (destformatdesc->type == FORMAT_UNKNOWN)
        return E_NOTIMPL;

    if (src_format == surfdesc.Format
            && dst_size.cx == src_size.cx
            && dst_size.cy == src_size.cy) /* Simple copy. */
    {
        UINT row_block_count = ((src_size.cx + srcformatdesc->block_width - 1) / srcformatdesc->block_width);
        UINT row_count = (src_size.cy + srcformatdesc->block_height - 1) / srcformatdesc->block_height;
        const BYTE *src_addr;
        BYTE *dst_addr;
        UINT row;

        if (src_rect->left & (srcformatdesc->block_width - 1)
                || src_rect->top & (srcformatdesc->block_height - 1)
                || (src_rect->right & (srcformatdesc->block_width - 1)
                    && src_size.cx != surfdesc.Width)
                || (src_rect->bottom & (srcformatdesc->block_height - 1)
                    && src_size.cy != surfdesc.Height))
        {
            WARN("Source rect %s is misaligned.\n", wine_dbgstr_rect(src_rect));
            return D3DXERR_INVALIDDATA;
        }

        if (FAILED(hr = IDirect3DSurface9_LockRect(dst_surface, &lockrect, dst_rect, 0)))
            return D3DXERR_INVALIDDATA;

        src_addr = src_memory;
        src_addr += (src_rect->top / srcformatdesc->block_height) * src_pitch;
        src_addr += (src_rect->left / srcformatdesc->block_width) * srcformatdesc->block_byte_count;
        dst_addr = lockrect.pBits;

        for (row = 0; row < row_count; ++row)
        {
            memcpy(dst_addr, src_addr, row_block_count * srcformatdesc->block_byte_count);
            src_addr += src_pitch;
            dst_addr += lockrect.Pitch;
        }

        IDirect3DSurface9_UnlockRect(dst_surface);
    }
    else /* Stretching or format conversion. */
    {
        if (srcformatdesc->bytes_per_pixel > 4)
            return E_NOTIMPL;
        if (destformatdesc->bytes_per_pixel > 4)
            return E_NOTIMPL;
        if (srcformatdesc->block_height != 1 || srcformatdesc->block_width != 1)
            return E_NOTIMPL;
        if (destformatdesc->block_height != 1 || destformatdesc->block_width != 1)
            return E_NOTIMPL;

        if (FAILED(hr = IDirect3DSurface9_LockRect(dst_surface, &lockrect, dst_rect, 0)))
            return D3DXERR_INVALIDDATA;

        if ((filter & 0xf) == D3DX_FILTER_NONE)
        {
            copy_simple_data(src_memory, src_pitch, src_size, srcformatdesc,
                    lockrect.pBits, lockrect.Pitch, dst_size, destformatdesc, color_key);
        }
        else /* if ((filter & 0xf) == D3DX_FILTER_POINT) */
        {
            if ((filter & 0xf) != D3DX_FILTER_POINT)
                FIXME("Unhandled filter %#x.\n", filter);

            /* Always apply a point filter until D3DX_FILTER_LINEAR,
             * D3DX_FILTER_TRIANGLE and D3DX_FILTER_BOX are implemented. */
            point_filter_simple_data(src_memory, src_pitch, src_size, srcformatdesc,
                    lockrect.pBits, lockrect.Pitch, dst_size, destformatdesc, color_key);
        }

        IDirect3DSurface9_UnlockRect(dst_surface);
    }

    return D3D_OK;
}

/************************************************************
 * D3DXLoadSurfaceFromSurface
 *
 * Copies the contents from one surface to another, performing any required
 * format conversion, resizing or filtering.
 *
 * PARAMS
 *   pDestSurface [I] pointer to the destination surface
 *   pDestPalette [I] palette to use
 *   pDestRect    [I] to be filled area of the surface
 *   pSrcSurface  [I] pointer to the source surface
 *   pSrcPalette  [I] palette used for the source surface
 *   pSrcRect     [I] area of the source data to load
 *   dwFilter     [I] filter to apply on resizing
 *   Colorkey     [I] any ARGB value or 0 to disable color-keying
 *
 * RETURNS
 *   Success: D3D_OK
 *   Failure: D3DERR_INVALIDCALL, if pDestSurface or pSrcSurface are NULL
 *            D3DXERR_INVALIDDATA, if one of the surfaces is not lockable
 *
 */
HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE9 pDestSurface,
                                          CONST PALETTEENTRY *pDestPalette,
                                          CONST RECT *pDestRect,
                                          LPDIRECT3DSURFACE9 pSrcSurface,
                                          CONST PALETTEENTRY *pSrcPalette,
                                          CONST RECT *pSrcRect,
                                          DWORD dwFilter,
                                          D3DCOLOR Colorkey)
{
    RECT rect;
    D3DLOCKED_RECT lock;
    D3DSURFACE_DESC SrcDesc;
    HRESULT hr;

    TRACE("(%p, %p, %p, %p, %p, %p, %u, %#x): relay\n", pDestSurface, pDestPalette, pDestRect,
        pSrcSurface, pSrcPalette, pSrcRect, dwFilter, Colorkey);

    if( !pDestSurface || !pSrcSurface ) return D3DERR_INVALIDCALL;

    IDirect3DSurface9_GetDesc(pSrcSurface, &SrcDesc);

    if( !pSrcRect ) SetRect(&rect, 0, 0, SrcDesc.Width, SrcDesc.Height);
    else rect = *pSrcRect;

    hr = IDirect3DSurface9_LockRect(pSrcSurface, &lock, NULL, D3DLOCK_READONLY);
    if(FAILED(hr)) return D3DXERR_INVALIDDATA;

    hr = D3DXLoadSurfaceFromMemory(pDestSurface, pDestPalette, pDestRect,
                                   lock.pBits, SrcDesc.Format, lock.Pitch,
                                   pSrcPalette, &rect, dwFilter, Colorkey);

    IDirect3DSurface9_UnlockRect(pSrcSurface);
    return hr;
}


HRESULT WINAPI D3DXSaveSurfaceToFileA(const char *dst_filename, D3DXIMAGE_FILEFORMAT file_format,
        IDirect3DSurface9 *src_surface, const PALETTEENTRY *src_palette, const RECT *src_rect)
{
    int len;
    WCHAR *filename;
    HRESULT hr;

    TRACE("(%s, %#x, %p, %p, %s): relay\n",
            wine_dbgstr_a(dst_filename), file_format, src_surface, src_palette, wine_dbgstr_rect(src_rect));

    if (!dst_filename) return D3DERR_INVALIDCALL;

    len = MultiByteToWideChar(CP_ACP, 0, dst_filename, -1, NULL, 0);
    filename = HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
    if (!filename) return E_OUTOFMEMORY;
    MultiByteToWideChar(CP_ACP, 0, dst_filename, -1, filename, len);

    hr = D3DXSaveSurfaceToFileW(filename, file_format, src_surface, src_palette, src_rect);

    HeapFree(GetProcessHeap(), 0, filename);
    return hr;
}

HRESULT WINAPI D3DXSaveSurfaceToFileW(const WCHAR *dst_filename, D3DXIMAGE_FILEFORMAT file_format,
        IDirect3DSurface9 *src_surface, const PALETTEENTRY *src_palette, const RECT *src_rect)
{
    IWICImagingFactory *factory;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IPropertyBag2 *encoder_options = NULL;
    IWICStream *stream = NULL;
    HRESULT hr;
    HRESULT initresult;
    const CLSID *encoder_clsid;
    const GUID *pixel_format_guid;
    WICPixelFormatGUID wic_pixel_format;
    D3DFORMAT d3d_pixel_format;
    D3DSURFACE_DESC src_surface_desc;
    D3DLOCKED_RECT locked_rect;
    int width, height;

    TRACE("(%s, %#x, %p, %p, %s)\n",
        wine_dbgstr_w(dst_filename), file_format, src_surface, src_palette, wine_dbgstr_rect(src_rect));

    if (!dst_filename || !src_surface) return D3DERR_INVALIDCALL;

    if (src_palette)
    {
        FIXME("Saving surfaces with palettized pixel formats not implemented yet\n");
        return D3DERR_INVALIDCALL;
    }

    switch (file_format)
    {
        case D3DXIFF_BMP:
            encoder_clsid = &CLSID_WICBmpEncoder;
            break;
        case D3DXIFF_PNG:
            encoder_clsid = &CLSID_WICPngEncoder;
            break;
        case D3DXIFF_JPG:
            encoder_clsid = &CLSID_WICJpegEncoder;
            break;
        case D3DXIFF_DDS:
        case D3DXIFF_DIB:
        case D3DXIFF_HDR:
        case D3DXIFF_PFM:
        case D3DXIFF_TGA:
        case D3DXIFF_PPM:
            FIXME("File format %#x is not supported yet\n", file_format);
            return E_NOTIMPL;
        default:
            return D3DERR_INVALIDCALL;
    }

    IDirect3DSurface9_GetDesc(src_surface, &src_surface_desc);
    if (src_rect)
    {
        if (src_rect->left == src_rect->right || src_rect->top == src_rect->bottom)
            return D3D_OK;
        if (src_rect->left < 0 || src_rect->top < 0)
            return D3DERR_INVALIDCALL;
        if (src_rect->left > src_rect->right || src_rect->top > src_rect->bottom)
            return D3DERR_INVALIDCALL;
        if (src_rect->right > src_surface_desc.Width || src_rect->bottom > src_surface_desc.Height)
            return D3DERR_INVALIDCALL;

        width = src_rect->right - src_rect->left;
        height = src_rect->bottom - src_rect->top;
    }
    else
    {
        width = src_surface_desc.Width;
        height = src_surface_desc.Height;
    }

    initresult = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        &IID_IWICImagingFactory, (void **)&factory);
    if (FAILED(hr)) goto cleanup_err;

    hr = IWICImagingFactory_CreateStream(factory, &stream);
    IWICImagingFactory_Release(factory);
    if (FAILED(hr)) goto cleanup_err;

    hr = IWICStream_InitializeFromFilename(stream, dst_filename, GENERIC_WRITE);
    if (FAILED(hr)) goto cleanup_err;

    hr = CoCreateInstance(encoder_clsid, NULL, CLSCTX_INPROC_SERVER,
        &IID_IWICBitmapEncoder, (void **)&encoder);
    if (FAILED(hr)) goto cleanup_err;

    hr = IWICBitmapEncoder_Initialize(encoder, (IStream *)stream, WICBitmapEncoderNoCache);
    IStream_Release((IStream *)stream);
    stream = NULL;
    if (FAILED(hr)) goto cleanup_err;

    hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, &encoder_options);
    if (FAILED(hr)) goto cleanup_err;

    hr = IWICBitmapFrameEncode_Initialize(frame, encoder_options);
    if (FAILED(hr)) goto cleanup_err;

    hr = IWICBitmapFrameEncode_SetSize(frame, width, height);
    if (FAILED(hr)) goto cleanup_err;

    pixel_format_guid = d3dformat_to_wic_guid(src_surface_desc.Format);
    if (!pixel_format_guid)
    {
        FIXME("Pixel format %#x is not supported yet\n", src_surface_desc.Format);
        hr = E_NOTIMPL;
        goto cleanup;
    }

    memcpy(&wic_pixel_format, pixel_format_guid, sizeof(GUID));
    hr = IWICBitmapFrameEncode_SetPixelFormat(frame, &wic_pixel_format);
    d3d_pixel_format = wic_guid_to_d3dformat(&wic_pixel_format);
    if (SUCCEEDED(hr) && d3d_pixel_format != D3DFMT_UNKNOWN)
    {
        TRACE("Using pixel format %s %#x\n", debugstr_guid(&wic_pixel_format), d3d_pixel_format);

        if (src_surface_desc.Format == d3d_pixel_format) /* Simple copy */
        {
            hr = IDirect3DSurface9_LockRect(src_surface, &locked_rect, src_rect, D3DLOCK_READONLY);
            if (SUCCEEDED(hr))
            {
                IWICBitmapFrameEncode_WritePixels(frame, height,
                    locked_rect.Pitch, height * locked_rect.Pitch, locked_rect.pBits);
                IDirect3DSurface9_UnlockRect(src_surface);
            }
        }
        else /* Pixel format conversion */
        {
            const PixelFormatDesc *src_format_desc, *dst_format_desc;
            SIZE size;
            DWORD dst_pitch;
            void *dst_data;

            src_format_desc = get_format_info(src_surface_desc.Format);
            dst_format_desc = get_format_info(d3d_pixel_format);
            if (src_format_desc->format == D3DFMT_UNKNOWN || dst_format_desc->format == D3DFMT_UNKNOWN)
            {
                FIXME("Unsupported pixel format conversion %#x -> %#x\n",
                    src_surface_desc.Format, d3d_pixel_format);
                hr = E_NOTIMPL;
                goto cleanup;
            }

            if (src_format_desc->bytes_per_pixel > 4
                || dst_format_desc->bytes_per_pixel > 4
                || src_format_desc->block_height != 1 || src_format_desc->block_width != 1
                || dst_format_desc->block_height != 1 || dst_format_desc->block_width != 1)
            {
                hr = E_NOTIMPL;
                goto cleanup;
            }

            size.cx = width;
            size.cy = height;
            dst_pitch = width * dst_format_desc->bytes_per_pixel;
            dst_data = HeapAlloc(GetProcessHeap(), 0, dst_pitch * height);
            if (!dst_data)
            {
                hr = E_OUTOFMEMORY;
                goto cleanup;
            }

            hr = IDirect3DSurface9_LockRect(src_surface, &locked_rect, src_rect, D3DLOCK_READONLY);
            if (SUCCEEDED(hr))
            {
                copy_simple_data(locked_rect.pBits, locked_rect.Pitch, size, src_format_desc,
                    dst_data, dst_pitch, size, dst_format_desc, 0);
                IDirect3DSurface9_UnlockRect(src_surface);
            }

            IWICBitmapFrameEncode_WritePixels(frame, height, dst_pitch, dst_pitch * height, dst_data);
            HeapFree(GetProcessHeap(), 0, dst_data);
        }

        hr = IWICBitmapFrameEncode_Commit(frame);
        if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit(encoder);
    }
    else WARN("Unsupported pixel format %#x\n", src_surface_desc.Format);

cleanup_err:
    if (FAILED(hr)) hr = D3DERR_INVALIDCALL;

cleanup:
    if (stream) IStream_Release((IStream *)stream);

    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (encoder_options) IPropertyBag2_Release(encoder_options);

    if (encoder) IWICBitmapEncoder_Release(encoder);

    if (SUCCEEDED(initresult)) CoUninitialize();

    return hr;
}
