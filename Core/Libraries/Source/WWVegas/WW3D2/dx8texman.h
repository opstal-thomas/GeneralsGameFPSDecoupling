/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : DX8 Texture Manager                                          *
 *                                                                                             *
 *                     $Archive:: /Commando/Code/ww3d2/dx8texman.h                            $*
 *                                                                                             *
 *              Original Author:: Hector Yee                                                   *
 *                                                                                             *
 *                       Author : Kenny Mitchell                                               *
 *                                                                                             *
 *                     $Modtime:: 06/27/02 1:27p                                              $*
 *                                                                                             *
 *                    $Revision:: 4                                                           $*
 *                                                                                             *
 * 06/27/02 KM Texture class abstraction																			*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#pragma once

#include "WWLib/always.h"
#include "texture.h"
#include "dx8wrapper.h"
#include "Renderer.h"
#include "ww3dformat.h"
#include "dx8list.h"
#include "WWLib/multilist.h"

class DX8TextureManagerClass;

class TextureTrackerClass : public MultiListObjectClass
{
public:
	TextureTrackerClass
	(
		unsigned int w,
		unsigned int h,
		MipCountType count,
		TextureBaseClass *tex
	)
	: Width(w),
	  Height(h),
	  Mip_level_count(count),
	  Texture(tex)
	{
	}

	virtual void Recreate() const =0;

	void Release()
	{
		Texture->Set_D3D_Base_Texture(nullptr);
	}

	TextureBaseClass* Get_Texture() const { return Texture; }


protected:

	unsigned int Width;
	unsigned int Height;
	MipCountType Mip_level_count;
	TextureBaseClass *Texture;
};

// One W3D texture tracker supports the three color texture dimensions.
// The type is retained through a device reset; recreating a cube or a volume
// as a 2D texture was incorrect and broke the resource contract.
class DX8TextureTrackerClass : public TextureTrackerClass
{
public:
    enum TextureKind
    {
        TRACK_2D,
        TRACK_CUBE,
        TRACK_VOLUME
    };

    DX8TextureTrackerClass(
        unsigned int w, unsigned int h, WW3DFormat format,
        MipCountType count, TextureBaseClass * tex, bool rt,
        TextureKind kind = TRACK_2D, unsigned int depth = 0)
        : TextureTrackerClass(w, h, count, tex),
          Format(format), RenderTarget(rt), Kind(kind), Depth(depth)
    {
    }

    virtual void Recreate() const override
    {
        WWASSERT(Texture->Peek_D3D_Base_Texture() == nullptr);
        if (Kind == TRACK_CUBE)
        {
            RenderTextureCubeDescription description;
            description.edgeLength = Width;
            description.pixelFormat = static_cast<unsigned>(Format);
            description.mipLevels = static_cast<unsigned>(Mip_level_count);
            description.memory = RENDER_TEXTURE_DEFAULT;
            description.renderTarget = RenderTarget;
            Texture->Poke_Texture(reinterpret_cast<IDirect3DBaseTexture8 *>(
                Renderer::Create_Texture_Cube(description)));
        }
        else if (Kind == TRACK_VOLUME)
        {
            RenderTextureVolumeDescription description;
            description.width = Width;
            description.height = Height;
            description.depth = Depth;
            description.pixelFormat = static_cast<unsigned>(Format);
            description.mipLevels = static_cast<unsigned>(Mip_level_count);
            description.memory = RENDER_TEXTURE_DEFAULT;
            Texture->Poke_Texture(reinterpret_cast<IDirect3DBaseTexture8 *>(
                Renderer::Create_Texture_Volume(description)));
        }
        else
        {
            RenderTexture2DDescription description;
            description.width = Width;
            description.height = Height;
            description.pixelFormat = static_cast<unsigned>(Format);
            description.mipLevels = static_cast<unsigned>(Mip_level_count);
            description.memory = RENDER_TEXTURE_DEFAULT;
            description.renderTarget = RenderTarget;
            Texture->Poke_Texture(reinterpret_cast<IDirect3DTexture8 *>(
                Renderer::Create_Texture_2D(description)));
        }
    }

private:
    WW3DFormat Format;
    bool RenderTarget;
    TextureKind Kind;
    unsigned int Depth;
};

class DX8ZTextureTrackerClass : public TextureTrackerClass
{
public:
	DX8ZTextureTrackerClass
	(
		unsigned int w,
		unsigned int h,
		WW3DZFormat zformat,
		MipCountType count,
		TextureBaseClass* tex
	)
	: TextureTrackerClass(w,h,count,tex), ZFormat(zformat)
	{
	}

	virtual void Recreate() const override
	{
		WWASSERT(Texture->Peek_D3D_Base_Texture()==nullptr);
		RenderTexture2DDescription description;
		description.width = Width;
		description.height = Height;
		description.pixelFormat = static_cast<unsigned>(ZFormat);
		description.mipLevels = static_cast<unsigned>(Mip_level_count);
		description.memory = RENDER_TEXTURE_DEFAULT;
		Texture->Poke_Texture(reinterpret_cast<IDirect3DTexture8 *>(
			Renderer::Create_Depth_Texture_2D(description)));
	}


private:
	WW3DZFormat ZFormat;
};


class DX8TextureManagerClass
{
public:
	static void Shutdown();
	static void Add(TextureTrackerClass *track);
	static void Remove(TextureBaseClass *tex);
	static void Release_Textures();
	static void Recreate_Textures();
private:
	static TextureTrackerList Managed_Textures;
};
