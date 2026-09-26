// Copyright (C) 2002-2012 Nikolaus Gebhardt
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#include "CImageLoaderJPG.h"

#ifdef _IRR_COMPILE_WITH_JPG_LOADER_

#include "IReadFile.h"
#include "CImage.h"
#include "os.h"
#include "irrString.h"
#ifdef _IRR_COMPILE_WITH_IMAGEIO_
#include <ImageIO/ImageIO.h>
#endif

namespace irr
{
namespace video
{

//! constructor
CImageLoaderJPG::CImageLoaderJPG()
{
	#ifdef _DEBUG
	setDebugName("CImageLoaderJPG");
	#endif
}



//! destructor
CImageLoaderJPG::~CImageLoaderJPG()
{
}



//! returns true if the file maybe is able to be loaded by this class
//! based on the file extension (e.g. ".tga")
bool CImageLoaderJPG::isALoadableFileExtension(const io::path& filename) const
{
	return core::hasFileExtension ( filename, "jpg", "jpeg" );
}


#ifdef _IRR_COMPILE_WITH_LIBJPEG_

    // struct for handling jpeg errors
    struct irr_jpeg_error_mgr
    {
        // public jpeg error fields
        struct jpeg_error_mgr pub;

        // for longjmp, to return to caller on a fatal error
        jmp_buf setjmp_buffer;

        // for having access to the filename when printing the error messages
        core::stringc* filename;
    };

void CImageLoaderJPG::init_source (j_decompress_ptr cinfo)
{
	// DO NOTHING
}



boolean CImageLoaderJPG::fill_input_buffer (j_decompress_ptr cinfo)
{
	// DO NOTHING
	return TRUE;
}



void CImageLoaderJPG::skip_input_data (j_decompress_ptr cinfo, long count)
{
	jpeg_source_mgr * src = cinfo->src;
	if(count > 0)
	{
		src->bytes_in_buffer -= count;
		src->next_input_byte += count;
	}
}



void CImageLoaderJPG::term_source (j_decompress_ptr cinfo)
{
	// DO NOTHING
}


void CImageLoaderJPG::error_exit (j_common_ptr cinfo)
{
	// unfortunately we need to use a goto rather than throwing an exception
	// as gcc crashes under linux crashes when using throw from within
	// extern c code

	// Always display the message
	(*cinfo->err->output_message) (cinfo);

	// cinfo->err really points to a irr_error_mgr struct
	irr_jpeg_error_mgr *myerr = (irr_jpeg_error_mgr*) cinfo->err;

	longjmp(myerr->setjmp_buffer, 1);
}


void CImageLoaderJPG::output_message(j_common_ptr cinfo)
{
	// display the error message.
	c8 temp1[JMSG_LENGTH_MAX];
	(*cinfo->err->format_message)(cinfo, temp1);
	core::stringc errMsg("JPEG FATAL ERROR in ");

	irr_jpeg_error_mgr* myerr = (irr_jpeg_error_mgr*)cinfo->err;
	errMsg += *myerr->filename;
	os::Printer::log(errMsg.c_str(),temp1, ELL_ERROR);
}
#endif // _IRR_COMPILE_WITH_LIBJPEG_

#ifdef _IRR_COMPILE_WITH_IMAGEIO_
//! Decodes into 32-bit pixels drawn in the picture's own colour space, so no colour matching changes
//! them, and keeps 24 bits of them as libjpeg does; grey keeps its space until spread over the channels,
//! CMYK and the rest are matched to sRGB
static IImage* loadImageIO(io::IReadFile* file)
{
	const long size = file->getSize();
	if (size < 3)
		return 0;

	CFMutableDataRef bytes = CFDataCreateMutable(0, size);
	CFDataSetLength(bytes, size);
	const bool read = file->read(CFDataGetMutableBytePtr(bytes), size) == (size_t)size;
	CGImageSourceRef source = read ? CGImageSourceCreateWithData(bytes, 0) : 0;
	CFRelease(bytes);
	CGImageRef picture = source ? CGImageSourceCreateImageAtIndex(source, 0, 0) : 0;
	if (source)
		CFRelease(source);
	if (!picture)
	{
		os::Printer::log("ImageIO could not decode the JPEG", file->getFileName(), ELL_ERROR);
		return 0;
	}

	const core::dimension2du dim((u32)CGImageGetWidth(picture), (u32)CGImageGetHeight(picture));
	//! The limits of libjpeg: 65500 a side and the 2 GB of IImage
	if (dim.Width > 65500 || dim.Height > 65500 ||
			!IImage::checkDataSizeLimit(IImage::getDataSizeFromFormat(ECF_A8R8G8B8, dim.Width, dim.Height)))
	{
		os::Printer::log("Image dimensions too large in file", file->getFileName(), ELL_ERROR);
		CGImageRelease(picture);
		return 0;
	}
	CGColorSpaceRef space = CGImageGetColorSpace(picture);
	const CGColorSpaceModel model = space ? CGColorSpaceGetModel(space) : kCGColorSpaceModelUnknown;
	IImage* image = new CImage(ECF_A8R8G8B8, dim);
	u8* grey = model == kCGColorSpaceModelMonochrome ? new u8[dim.getArea()] : 0;
	CGColorSpaceRef target = grey || model == kCGColorSpaceModelRGB ? CGColorSpaceRetain(space) :
			CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	CGContextRef context = grey ?
			CGBitmapContextCreate(grey, dim.Width, dim.Height, 8, dim.Width, target, kCGImageAlphaNone) :
			CGBitmapContextCreate(image->getData(), dim.Width, dim.Height, 8, image->getPitch(), target,
					kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
	CGColorSpaceRelease(target);
	if (context)
	{
		CGContextSetBlendMode(context, kCGBlendModeCopy);
		CGContextSetInterpolationQuality(context, kCGInterpolationNone);
		CGContextDrawImage(context, CGRectMake(0, 0, dim.Width, dim.Height), picture);
		CGContextRelease(context);
	}
	CGImageRelease(picture);
	if (!context)
	{
		os::Printer::log("ImageIO could not draw the JPEG", file->getFileName(), ELL_ERROR);
		delete [] grey;
		image->drop();
		return 0;
	}

	if (grey)
	{
		u32* pixel = (u32*)image->getData();
		for (u32 i = 0; i < dim.getArea(); ++i)
			pixel[i] = 0xFF000000u | grey[i] << 16 | grey[i] << 8 | grey[i];
		delete [] grey;
	}
	IImage* rgb = new CImage(ECF_R8G8B8, dim);
	image->copyToScaling(rgb->getData(), dim.Width, dim.Height, ECF_R8G8B8, rgb->getPitch());
	image->drop();
	return rgb;
}
#endif

//! returns true if the file maybe is able to be loaded by this class
bool CImageLoaderJPG::isALoadableFileFormat(io::IReadFile* file) const
{
	#if !defined(_IRR_COMPILE_WITH_LIBJPEG_) && !defined(_IRR_COMPILE_WITH_IMAGEIO_)
	return false;
	#else

	if (!(file && file->seek(0)))
		return false;
	unsigned char header[3];
	size_t headerLen = file->read(header, sizeof(header));
	return headerLen >= 3 && !memcmp(header, "\xFF\xD8\xFF", 3);
	#endif
}

//! creates a surface from the file
IImage* CImageLoaderJPG::loadImage(io::IReadFile* file) const
{
	#if defined(_IRR_COMPILE_WITH_IMAGEIO_)
	return file ? loadImageIO(file) : 0;
	#elif !defined(_IRR_COMPILE_WITH_LIBJPEG_)
	os::Printer::log("Can't load as not compiled with _IRR_COMPILE_WITH_LIBJPEG_", file->getFileName(), ELL_DEBUG);
	return 0;
	#else

	if (!file)
		return 0;

	core::stringc filename = file->getFileName();
	long fileSize = file->getSize();
	if ( fileSize < 3 )
		return 0;

	u8 **rowPtr=0;
	u8* input = new u8[fileSize];
	file->read(input, fileSize);

	// allocate and initialize JPEG decompression object
	struct jpeg_decompress_struct cinfo;
	struct irr_jpeg_error_mgr jerr;

	//We have to set up the error handler first, in case the initialization
	//step fails.  (Unlikely, but it could happen if you are out of memory.)
	//This routine fills in the contents of struct jerr, and returns jerr's
	//address which we place into the link field in cinfo.

	cinfo.err = jpeg_std_error(&jerr.pub);
	cinfo.err->error_exit = error_exit;
	cinfo.err->output_message = output_message;
	jerr.filename = &filename;

	// compatibility fudge:
	// we need to use setjmp/longjmp for error handling as gcc-linux
	// crashes when throwing within external c code
	if (setjmp(jerr.setjmp_buffer))
	{
		// If we get here, the JPEG code has signaled an error.
		// We need to clean up the JPEG object and return.

		jpeg_destroy_decompress(&cinfo);

		delete [] input;
		delete [] rowPtr;

		// return null pointer
		return 0;
	}

	// Now we can initialize the JPEG decompression object.
	jpeg_create_decompress(&cinfo);

	// specify data source
	jpeg_source_mgr jsrc;

	// Set up data pointer
	jsrc.bytes_in_buffer = fileSize;
	jsrc.next_input_byte = (JOCTET*)input;
	cinfo.src = &jsrc;

	jsrc.init_source = init_source;
	jsrc.fill_input_buffer = fill_input_buffer;
	jsrc.skip_input_data = skip_input_data;
	jsrc.resync_to_restart = jpeg_resync_to_restart;
	jsrc.term_source = term_source;

	// Decodes JPG input from whatever source
	// Does everything AFTER jpeg_create_decompress
	// and BEFORE jpeg_destroy_decompress
	// Caller is responsible for arranging these + setting up cinfo

	// read file parameters with jpeg_read_header()
	jpeg_read_header(&cinfo, TRUE);

	bool useCMYK=false;
	if (cinfo.jpeg_color_space==JCS_CMYK || cinfo.jpeg_color_space==JCS_YCCK)
	{
		cinfo.out_color_space=JCS_CMYK;
		cinfo.out_color_components=4;
		useCMYK=true;
	}
	else
	{
		cinfo.out_color_space=JCS_RGB;
		cinfo.out_color_components=3;
	}
	cinfo.output_gamma=2.2;
	cinfo.do_fancy_upsampling=FALSE;

	// Start decompressor
	jpeg_start_decompress(&cinfo);

	// Get image data
	u32 rowspan = cinfo.image_width * cinfo.out_color_components;
	u32 width = cinfo.image_width;
	u32 height = cinfo.image_height;

	if (	width > JPEG_MAX_DIMENSION || height > JPEG_MAX_DIMENSION 
		|| !IImage::checkDataSizeLimit(IImage::getDataSizeFromFormat(ECF_R8G8B8, width, height))
		)
	{
		os::Printer::log("Image dimensions too large in file", filename, ELL_ERROR);
		longjmp(jerr.setjmp_buffer, 1);
	}


	// Allocate memory for buffer
	u8* output = new u8[rowspan * height];

	// Here we use the library's state variable cinfo.output_scanline as the
	// loop counter, so that we don't have to keep track ourselves.
	// Create array of row pointers for lib
	rowPtr = new u8* [height];

	for( size_t i = 0; i < height; i++ )
		rowPtr[i] = &output[ i * rowspan ];

	u32 rowsRead = 0;

	while( cinfo.output_scanline < cinfo.output_height )
		rowsRead += jpeg_read_scanlines( &cinfo, &rowPtr[rowsRead], cinfo.output_height - rowsRead );

	delete [] rowPtr;
	rowPtr = 0;
	// Finish decompression

	jpeg_finish_decompress(&cinfo);

	// Release JPEG decompression object
	// This is an important step since it will release a good deal of memory.
	jpeg_destroy_decompress(&cinfo);

	// convert image
	IImage* image = 0;
	if (useCMYK)
	{
		image = new CImage(ECF_R8G8B8,
				core::dimension2d<u32>(width, height));
		const u32 size = 3*width*height;
		u8* data = (u8*)image->getData();
		if (data)
		{
			for (u32 i=0,j=0; i<size; i+=3, j+=4)
			{
				// Also works without K, but has more contrast with K multiplied in
//				data[i+0] = output[j+2];
//				data[i+1] = output[j+1];
//				data[i+2] = output[j+0];
				data[i+0] = (char)(output[j+2]*(output[j+3]/255.f));
				data[i+1] = (char)(output[j+1]*(output[j+3]/255.f));
				data[i+2] = (char)(output[j+0]*(output[j+3]/255.f));
			}
		}
		delete [] output;
	}
	else
		image = new CImage(ECF_R8G8B8,
				core::dimension2d<u32>(width, height), output);

	delete [] input;

	return image;

	#endif
}



//! creates a loader which is able to load jpeg images
IImageLoader* createImageLoaderJPG()
{
	return new CImageLoaderJPG();
}

} // end namespace video
} // end namespace irr

#endif

