// Copyright (C) 2009-2012 Gaz Davidson
// This file is part of the "Irrlicht Engine".
// For conditions of distribution and use, see copyright notice in irrlicht.h

#ifndef IRR_C_TAR_READER_H_INCLUDED
#define IRR_C_TAR_READER_H_INCLUDED

#include "IrrCompileConfig.h"

#ifdef __IRR_COMPILE_WITH_TAR_ARCHIVE_LOADER_

#include "IReadFile.h"
#include "IFileSystem.h"
#include "CFileList.h"

namespace irr
{
namespace io
{

	enum E_TAR_LINK_INDICATOR
	{
		ETLI_REGULAR_FILE_OLD      =  0 ,
		ETLI_REGULAR_FILE          = '0',
		ETLI_LINK_TO_ARCHIVED_FILE = '1', // Hard link
		ETLI_SYMBOLIC_LINK         = '2',
		ETLI_CHAR_SPECIAL_DEVICE   = '3',
		ETLI_BLOCK_SPECIAL_DEVICE  = '4',
		ETLI_DIRECTORY             = '5',
		ETLI_FIFO_SPECIAL_FILE     = '6',
		ETLI_CONTIGUOUS_FILE       = '7'
	};

// byte-align structures
#include "irrpack.h"

	struct STarHeader
	{
		c8 FileName[100];
		c8 FileMode[8];
		c8 UserID[8];
		c8 GroupID[8];
		c8 Size[12];
		c8 ModifiedTime[12];
		c8 Checksum[8];
		c8 Link;
		c8 LinkName[100];
		c8 Magic[6];
		c8 USTARVersion[2];
		c8 UserName[32];
		c8 GroupName[32];
		c8 DeviceMajor[8];
		c8 DeviceMinor[8];
		c8 FileNamePrefix[155];
	} PACK_STRUCT;

// Default alignment
#include "irrunpack.h"

	//! Archiveloader capable of loading ZIP Archives
	class CArchiveLoaderTAR : public IArchiveLoader
	{
	public:

		//! Constructor
		CArchiveLoaderTAR(io::IFileSystem* fs);

		//! returns true if the file maybe is able to be loaded by this class
		//! based on the file extension (e.g. ".tar")
		virtual bool isALoadableFileFormat(const io::path& filename) const IRR_OVERRIDE;

		//! Check if the file might be loaded by this class
		/** Check might look into the file.
		\param file File handle to check.
		\return True if file seems to be loadable. */
		virtual bool isALoadableFileFormat(io::IReadFile* file) const IRR_OVERRIDE;

		//! Check to see if the loader can create archives of this type.
		/** Check based on the archive type.
		\param fileType The archive type to check.
		\return True if the archile loader supports this type, false if not */
		virtual bool isALoadableFileFormat(E_FILE_ARCHIVE_TYPE fileType) const IRR_OVERRIDE;

		//! Creates an archive from the filename
		/** \param file File handle to check.
		\return Pointer to newly created archive, or 0 upon error. */
		virtual IFileArchive* createArchive(const io::path& filename, bool ignoreCase, bool ignorePaths) const IRR_OVERRIDE;

		//! creates/loads an archive from the file.
		//! \return Pointer to the created archive. Returns 0 if loading failed.
		virtual io::IFileArchive* createArchive(io::IReadFile* file, bool ignoreCase, bool ignorePaths) const IRR_OVERRIDE;

	private:
		io::IFileSystem* FileSystem;
	};



	class CTarReader : public virtual IFileArchive, virtual CFileList
	{
	public:

		CTarReader(IReadFile* file, bool ignoreCase, bool ignorePaths);

		virtual ~CTarReader();

		//! opens a file by file name
		virtual IReadFile* createAndOpenFile(const io::path& filename) IRR_OVERRIDE;

		//! opens a file by index
		virtual IReadFile* createAndOpenFile(u32 index) IRR_OVERRIDE;

		//! returns the list of files
		virtual const IFileList* getFileList() const IRR_OVERRIDE;

		//! get the class Type
		virtual E_FILE_ARCHIVE_TYPE getType() const IRR_OVERRIDE { return EFAT_TAR; }

		//! return the name (id) of the file Archive
		virtual const io::path& getArchiveName() const  IRR_OVERRIDE {return Path;}

	private:

		u32 populateFileList();

		IReadFile* File;
	};

} // end namespace io
} // end namespace irr

#endif // __IRR_COMPILE_WITH_TAR_ARCHIVE_LOADER_
#endif // IRR_C_TAR_READER_H_INCLUDED
