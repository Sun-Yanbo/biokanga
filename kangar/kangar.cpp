// genreads.cpp : Defines the entry point for the console application.
// Executable needs to be renamed as kangar
// Purpose -
// Loads short reads generated by sequencers - e.g. Illumia - and processes these after user specified filtering into an output file
// for subsequent efficient pipeline processing
//
// Current limits are that the packed reads need to fit into memory
// Release 1.10.0	Restructured build directories to reflect application name
// Release 1.11.0   public binary release
// Release 1.12.0   public binary release
// Release 1.12.1   fix for dumping sequences as fasta/fastq
// Release 1.12.2   changed _MAX_PATH to 260 to maintain compatibility with windows
// Release 2.1.0    public release

#include "stdafx.h"

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if _WIN32
#include <process.h>
#include "../libbiokanga/commhdrs.h"
#else
#include <sys/mman.h>
#include <pthread.h>
#include "../libbiokanga/commhdrs.h"
#endif

const char *cpszProgVer = "2.1.0";		// increment with each release


teBSFrsltCodes Process(etPRRMode PMode,		// processing mode
   		UINT32 NumReadsLimit,				// limit processing to this many reads
		bool bKeepDups,						// true if duplicate reads not to be filtered out
		etFQMethod Quality,					// fastq quality value method
		int Trim5,							// trim this many bases off leading sequence 5' end
		int Trim3,							// trim this many bases off trailing sequence 3' end
		int NumInputFileSpecs,				// number of input file specs
		char *pszInfileSpecs[],				// names of inputs file (wildcards allowed unless in dump mode) containing raw reads
		char *pszInPairFile,				// if paired reads processing then file containing paired ends
		char *pszOutFile);					// output into this file only if not NULL or not '\0'


CStopWatch gStopWatch;
CDiagnostics gDiagnostics;				// for writing diagnostics messages to log file
char gszProcName[_MAX_FNAME];			// process name
bool gbActivity;						// used to determine if activity messages vi printf's can be used - output activity if eDLInfo or less

#ifdef _WIN32
// required by str library
#if !defined(__AFX_H__)  ||  defined(STR_NO_WINSTUFF)
HANDLE STR_get_stringres()
{
	return NULL;	//Works for EXEs; in a DLL, return the instance handle
}
#endif

const STRCHAR* STR_get_debugname()
{
	return _T("kangar");
}
// end of str library required code
#endif


#ifdef _WIN32
int _tmain(int argc, char* argv[])
{
// determine my process name
_splitpath(argv[0],NULL,NULL,gszProcName,NULL);
#else
int
main(int argc, const char** argv)
{
// determine my process name
CUtility::splitpath((char *)argv[0],NULL,gszProcName);
#endif
int iScreenLogLevel;		// level of file diagnostics
int iFileLogLevel;			// level of file diagnostics
char szLogFile[_MAX_PATH];	// write diagnostics to this file
int Rslt = 0;   			// function result code >= 0 represents success, < 0 on failure
int Idx;					// general iteration indexer

etPRRMode PMode;				// processing mode
bool bKeepDups;				// true if duplicate reads are to be retained
int NumInputFiles;			// number of input files
char *pszInfileSpecs[cRRMaxInFileSpecs];  // input (wildcards allowed if single ended) raw sequencer read files
char szInPairfile[_MAX_PATH];  // input raw sequencer paired end read file
char szOutfile[_MAX_PATH];  // output rds file or csv if user requested stats

etFQMethod Quality;			// quality scoring for fastq sequence files
int NumReadsLimit;			// only process this many reads, useful whilst testing workflow
int Trim5;					// trim this many bases off leading sequence 5' end
int Trim3;					// trim this many bases off trailing sequence 3' end


// command line args
struct arg_lit  *help    = arg_lit0("hH","help",                "print this help and exit");
struct arg_lit  *version = arg_lit0("v","version,ver",			"print version information and exit");
struct arg_int *FileLogLevel=arg_int0("f", "FileLogLevel",		"<int>","Level of diagnostics written to screen and logfile 0=fatal,1=errors,2=info,3=diagnostics,4=debug");
struct arg_file *LogFile = arg_file0("F","log","<file>",		"diagnostics log file");
struct arg_int *pmode = arg_int0("m","mode","<int>",		    "processing mode: 0 - single end create, 1 - paired end create, 2 - output statistics, 3 - dump as fasta (default = 0)");
struct arg_lit  *keepdups = arg_lit0("k","removedups",			"remove duplicate reads retaining only one (default is not to remove any duplicated reads)");
struct arg_int *qual = arg_int0("q","quality","<int>",		    "fastq quality scoring - 0 - Sanger, 1 = Illumina 1.3+, 2 = Solexa < 1.3, 3 = Ignore quality (default = 3)");

struct arg_int *trim5 = arg_int0("t","trim5","<int>",		    "trim this many bases off leading sequence 5' end (default = 0)");
struct arg_int *trim3 = arg_int0("T","trim3","<int>",		    "trim this many bases off trailing sequence 3' end (default = 0)");

struct arg_file *inreadfiles = arg_filen("i","in","<file>",0,cRRMaxInFileSpecs,"input from these raw sequencer read files, wildcards allowed if single ended");
struct arg_file *inpairreadfile = arg_file0("u","pair","<file>",	"if paired end processing then input read pairs from this paired end file");
struct arg_file *outfile = arg_file1("o","out","<file>",		"output accepted processed reads to this file");
struct arg_int *readslimit = arg_int0("n","numreadslimit","<int>","limit number of reads (or dumps) in each input file to this many - 0 (default) if no limit");

struct arg_end *end = arg_end(20);

void *argtable[] = {help,version,FileLogLevel,LogFile,
					pmode,keepdups,qual,trim5,trim3,inreadfiles,inpairreadfile,outfile,readslimit,
					end};

char **pAllArgs;
int argerrors;
argerrors = CUtility::arg_parsefromfile(argc,(char **)argv,&pAllArgs);
if(argerrors >= 0)
	argerrors = arg_parse(argerrors,pAllArgs,argtable);

/* special case: '--help' takes precedence over error reporting */
if (help->count > 0)
        {
		printf("\n%s the K-mer Adaptive Next Generation Aligner Reads preprocessor, Version %s\nOptions ---\n", gszProcName,cpszProgVer);
        arg_print_syntax(stdout,argtable,"\n");
        arg_print_glossary(stdout,argtable,"  %-25s %s\n");
		printf("\nNote: Parameters can be entered into a parameter file, one parameter per line.");
		printf("\n      To invoke this parameter file then precede it's name with '@'");
		printf("\n      e.g. %s @myparams.txt\n",gszProcName);
		printf("\nPlease report any issues regarding usage of %s at https://github.com/csiro-crop-informatics/biokanga/issues\n\n",gszProcName);
		exit(1);
        }

    /* special case: '--version' takes precedence error reporting */
if (version->count > 0)
        {
		printf("\n%s Version %s\n",gszProcName,cpszProgVer);
		exit(1);
        }

if (!argerrors)
	{
	gbActivity = true;
	if(FileLogLevel->count && !LogFile->count)
		{
		printf("\nError: FileLogLevel '-f%d' specified but no logfile '-F<logfile>'",FileLogLevel->ival[0]);
		exit(1);
		}

	iScreenLogLevel = iFileLogLevel = FileLogLevel->count ? FileLogLevel->ival[0] : eDLInfo;
	if(iFileLogLevel < eDLNone || iFileLogLevel > eDLDebug)
		{
		printf("\nError: FileLogLevel '-l%d' specified outside of range %d..%d",iFileLogLevel,eDLNone,eDLDebug);
		exit(1);
		}
	if(LogFile->count)
		{
		strncpy(szLogFile,LogFile->filename[0],_MAX_PATH);
		szLogFile[_MAX_PATH-1] = '\0';
		}
	else
		{
		iFileLogLevel = eDLNone;
		szLogFile[0] = '\0';
		}

			// now that log parameters have been parsed then initialise diagnostics log system
	if(!gDiagnostics.Open(szLogFile,(etDiagLevel)iScreenLogLevel,(etDiagLevel)iFileLogLevel,true))
		{
		printf("\nError: Unable to start diagnostics subsystem\n");
		if(szLogFile[0] != '\0')
			printf(" Most likely cause is that logfile '%s' can't be opened/created\n",szLogFile);
		exit(1);
		}
	gDiagnostics.DiagOut(eDLInfo,gszProcName,"Version: %s",cpszProgVer);


	PMode = (etPRRMode)(pmode->count ? pmode->ival[0] : ePMRRNewSingle);
	if(PMode < 0 || PMode >= ePMRRplaceholder)
		{
		gDiagnostics.DiagOut(eDLFatal,gszProcName,"Processing mode '-m%d' specified outside of range %d..%d",PMode,0,(int)ePMRRplaceholder-1);
		exit(1);
		}


	if(PMode == ePMRRNewSingle)
		bKeepDups = keepdups->count ? false : true;
	else
		{
		if(keepdups->count)
			{
			gDiagnostics.DiagOut(eDLFatal,gszProcName,"Sorry, duplicate removal '-k' currently not supported when processing paired end reads in '-m1' processing mode");
			exit(1);
			}
		bKeepDups = true;
		}
	Quality = (etFQMethod)(qual->count ? qual->ival[0] : eFQIgnore);
	if(Quality < eFQSanger || Quality >= eFQplaceholder)
		{
		gDiagnostics.DiagOut(eDLFatal,gszProcName,"Error: fastq quality '-q%d' specified outside of range %d..%d",Quality,eFQSanger,(int)eFQplaceholder-1);
		exit(1);
		}

	if(PMode == ePMRRNewSingle || PMode == ePMRRNewPaired)
		{
		Trim5 = trim5->count ? trim5->ival[0] : 0;
		if(Trim5 < 0 || Trim5 > 99)
			{
			gDiagnostics.DiagOut(eDLFatal,gszProcName,"Error: 5' end trim '-t%d' specified outside of range 0..99",Trim5);
			exit(1);
			}
		Trim3 = trim3->count ? trim3->ival[0] : 0;
		if(Trim3 < 0 || Trim3 > 99)
			{
			gDiagnostics.DiagOut(eDLFatal,gszProcName,"Error: 3' end trim '-t%d' specified outside of range 0..99",Trim3);
			exit(1);
			}
		}
	else
		{
		Trim5 = 0;
		Trim3 = 0;
		}

	NumReadsLimit = readslimit->count ? readslimit->ival[0] : 0;
	NumReadsLimit &= 0x7fffffff;	// simply to ensure limit can't be later taken as a negative value...

	if(PMode != ePMRRNewPaired && inpairreadfile->count)
		{
		gDiagnostics.DiagOut(eDLFatal,gszProcName,"Error: Can't specify paired end file '-I%s' if not processing for paired end with '-m%d'",inpairreadfile->filename[0],PMode);
		exit(1);
		}
	if(PMode == ePMRRNewPaired)
		{
		if(!inpairreadfile->count)
			{
			gDiagnostics.DiagOut(eDLFatal,gszProcName,"Error: Must specify paired end file with '-I<file>' if processing for paired end with '-m%d'",PMode);
			exit(1);
			}
		strncpy(szInPairfile,inpairreadfile->filename[0],_MAX_PATH);
		szInPairfile[_MAX_PATH-1] = '\0';
		}
	else
		szInPairfile[0] = '\0';

	if(!inreadfiles->count)
		{
		printf("\nError: No input file%s specified with with '-i<filespec>' option)", PMode >= ePMRRStats ? "" : "(s)");
		exit(1);
		}

	if(PMode == ePMRRNewPaired && inreadfiles->count > 1)
		{
		gDiagnostics.DiagOut(eDLFatal,gszProcName,"Error: Paired end processing requested but more than one input raw reads file specified with '-i<file>'");
		exit(1);
		}

	for(NumInputFiles=Idx=0;NumInputFiles < cRRMaxInFileSpecs && Idx < inreadfiles->count; Idx++)
		{
		pszInfileSpecs[Idx] = NULL;
		if(pszInfileSpecs[NumInputFiles] == NULL)
			pszInfileSpecs[NumInputFiles] = new char [_MAX_PATH];
		strncpy(pszInfileSpecs[NumInputFiles],inreadfiles->filename[Idx],_MAX_PATH);
		pszInfileSpecs[NumInputFiles][_MAX_PATH-1] = '\0';
		CUtility::TrimQuotedWhitespcExtd(pszInfileSpecs[NumInputFiles]);
		if(pszInfileSpecs[NumInputFiles][0] != '\0')
			NumInputFiles++;
		}

	if(!NumInputFiles)
		{
		gDiagnostics.DiagOut(eDLFatal,gszProcName,"Error: After removal of whitespace, no input file%s specified with '-i<filespec>' option)", PMode >= ePMRRStats ? "" : "(s)");
		exit(1);
		}

	if(PMode == ePMRRStats && NumInputFiles > 1)
		{
		gDiagnostics.DiagOut(eDLFatal,gszProcName,"Error: Only one input file can be specified with '-i<filespec>' when generating stats with '-m%d'",ePMRRStats);
		exit(1);
		}

	strncpy(szOutfile,outfile->filename[0],_MAX_PATH);
	szOutfile[_MAX_PATH-1] = '\0';
	CUtility::TrimQuotedWhitespcExtd(szOutfile);
	if(szOutfile[0] == '\0')
		{
		gDiagnostics.DiagOut(eDLFatal,gszProcName,"Error: No output file ('-o<file> option') specified after removal of leading/trailing quotes and whitespace");
		exit(1);
		}

	gDiagnostics.DiagOut(eDLInfo,gszProcName,"Processing parameters:");

	const char *pszProcMode;
	switch(PMode) {
		case ePMRRNewSingle:
			pszProcMode = "Create new processed single end reads file from raw reads files";
			break;

		case ePMRRNewPaired:
			pszProcMode = "Create new processed paired end reads file from raw reads files";
			break;

		case ePMRRStats:
			pszProcMode = "Generate CSV statistics file from contents of processed reads file";
			break;

		case ePMRRDumpFasta:
			pszProcMode = "Generate fasta file from contents of processed reads file";
			break;

		default:
			pszProcMode = "Unknown mode, defaulting to creating single end reads reads file from raw reads files";
			PMode = ePMRRNewSingle;
			break;
		};
	gDiagnostics.DiagOutMsgOnly(eDLInfo,"Processing mode: '%s'",pszProcMode);

	gDiagnostics.DiagOutMsgOnly(eDLInfo,"Retain duplicate sequences: %s",bKeepDups ? "Yes" : "No");

	switch(Quality) {
		case eFQSanger:
			pszProcMode = "Sanger Phred";
			break;

		case eFQIllumia:
			pszProcMode = "Illumina 1.3+ Phred";
			break;

		case eFQSolexa:
			pszProcMode = "Solexa/Illumia pre-1.3";
			break;

		case eFQIgnore:
			pszProcMode = "ignore";
			break;

		default:
			pszProcMode = "Unknown scoring, defaulting to ignore";
			Quality = eFQIgnore;
			break;
		};

	if(NumReadsLimit > 0)
		gDiagnostics.DiagOutMsgOnly(eDLInfo,"limiting processing to the first %d reads",NumReadsLimit);

	gDiagnostics.DiagOutMsgOnly(eDLInfo,"Fastq quality scoring method: '%s'",pszProcMode);

	gDiagnostics.DiagOutMsgOnly(eDLInfo,"Trim %d bases from 5' end and %d bases from 3' end",Trim5,Trim3);

	for(Idx=0; Idx < NumInputFiles; Idx++)
		gDiagnostics.DiagOutMsgOnly(eDLInfo,"input raw reads files (%d): '%s'",Idx+1,pszInfileSpecs[Idx]);

	if(szInPairfile[0] != '\0')
		gDiagnostics.DiagOutMsgOnly(eDLInfo,"input raw paired reads files: '%s'", szInPairfile);

	gDiagnostics.DiagOutMsgOnly(eDLInfo,"output file to create: '%s'", szOutfile);
#ifdef _WIN32
	SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
#endif
	gStopWatch.Start();
	Rslt = Process(PMode,			// processing mode
					NumReadsLimit,				// limit processing to this many reads
					bKeepDups,
					Quality,					// fastq quality scoring method
					Trim5,						// trim this many bases off leading sequence 5' end
					Trim3,						// trim this many bases off trailing sequence 3' end
					NumInputFiles,				// number of input file specs
					pszInfileSpecs,				// names of inputs file (wildcards allowed unless in dump mode) containing raw reads
					szInPairfile,				// name of paired end file containing paired read to those in pszInFileSpecs
					szOutfile);					// output into this file

	gStopWatch.Stop();
	Rslt = Rslt >=0 ? 0 : 1;
	gDiagnostics.DiagOut(eDLInfo,gszProcName,"Exit code: %d Total processing time: %s",Rslt,gStopWatch.Read());
	exit(Rslt);
	}
else
	{
	printf("\n%s the K-mer Adaptive Next Generation Aligner Reads preprocessor, Version %s\n", gszProcName,cpszProgVer);
	arg_print_errors(stdout,end,gszProcName);
	arg_print_syntax(stdout,argtable,"\nUse '-h' to view option and parameter usage\n");
	exit(1);
	}
return 0;
}

teBSFrsltCodes
Process(etPRRMode PMode,						// processing mode
		UINT32 NumReadsLimit,				// limit processing to this many reads
		bool bKeepDups,						// true if duplicate reads not to be filtered out
		etFQMethod Quality,					// fastq quality value method
		int Trim5,							// trim this many bases off leading sequence 5' end
		int Trim3,							// trim this many bases off trailing sequence 3' end
		int NumInputFileSpecs,				// number of input file specs
		char *pszInfileSpecs[],				// names of inputs file (wildcards allowed unless in dump mode) containing raw reads
		char *pszInPairFile,				// if paired reads processing then file containing paired ends
		char *pszOutFile)					// output into this file
{
teBSFrsltCodes Rslt;
CProcRawReads RawReads;
RawReads.ReportActivity(true);

switch(PMode) {
	case ePMRRNewSingle:
	case ePMRRNewPaired:
		Rslt = RawReads.LoadAndProcessReads(PMode,NumReadsLimit,bKeepDups,Quality,Trim5,Trim3,NumInputFileSpecs,pszInfileSpecs,pszInPairFile,pszOutFile);
		break;

	case ePMRRStats:
		Rslt = RawReads.GenStats(PMode,			// processing mode
					pszInfileSpecs[0],			// input file
					pszOutFile);				// output into this file
		break;

	case ePMRRDumpFasta:
		Rslt = RawReads.GenDump(PMode,				// processing mode
					NumReadsLimit,				// limit processing to this many reads
					pszInfileSpecs[0],			// input file
					pszOutFile);				// output into this file
		break;
	}
return(Rslt);
}




