/*
 * 				Shell Library Functions
 *
 * Copyright 1998 Marcus Meissner
 * Copyright 2002 Eric Pouech
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <ctype.h>
#include <assert.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winreg.h"
#include "winuser.h"
#include "shlwapi.h"
#include "ddeml.h"

#include "wine/winbase16.h"
#include "shell32_main.h"
#include "pidl.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(exec);

static const WCHAR wszOpen[] = {'o','p','e','n',0};
static const WCHAR wszExe[] = {'.','e','x','e',0};
static const WCHAR wszILPtr[] = {':','%','p',0};
static const WCHAR wszShell[] = {'\\','s','h','e','l','l','\\',0};
static const WCHAR wszFolder[] = {'F','o','l','d','e','r',0};
static const WCHAR wszEmpty[] = {0};

#define SEE_MASK_CLASSALL (SEE_MASK_CLASSNAME | SEE_MASK_CLASSKEY)


/***********************************************************************
 *	SHELL_ArgifyW [Internal]
 *
 * this function is supposed to expand the escape sequences found in the registry
 * some diving reported that the following were used:
 * + %1, %2...  seem to report to parameter of index N in ShellExecute pmts
 *	%1 file
 *	%2 printer
 *	%3 driver
 *	%4 port
 * %I address of a global item ID (explorer switch /idlist)
 * %L seems to be %1 as long filename followed by the 8+3 variation
 * %S ???
 * %* all following parameters (see batfile)
 *
 * FIXME: use 'len'
 * FIXME: Careful of going over string boundaries. No checking is done to 'res'...
 */
static BOOL SHELL_ArgifyW(WCHAR* out, int len, const WCHAR* fmt, const WCHAR* lpFile, LPITEMIDLIST pidl, LPCWSTR args)
{
    WCHAR   xlpFile[1024];
    BOOL    done = FALSE;
    BOOL    found_p1 = FALSE;
    PWSTR   res = out;
    PCWSTR  cmd;
    LPVOID  pv;

    TRACE("%p, %d, %s, %s, %p, %p\n", out, len, debugstr_w(fmt),
          debugstr_w(lpFile), pidl, args);

    while (*fmt)
    {
        if (*fmt == '%')
        {
            switch (*++fmt)
            {
            case '\0':
            case '%':
                *res++ = '%';
                break;

            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case '0':
            case '*':
                if (args)
                {
                    if (*fmt == '*')
                    {
                        *res++ = '"';
                        while(*args)
                            *res++ = *args++;
                        *res++ = '"';
                    }
                    else
                    {
                        while(*args && !isspace(*args))
                            *res++ = *args++;

                        while(isspace(*args))
                            ++args;
                    }
                    break;
                }
                /* else fall through */
            case '1':
                if (!done || (*fmt == '1'))
                {
                    /*FIXME Is the call to SearchPathW() really needed? We already have separated out the parameter string in args. */
                    if (SearchPathW(NULL, lpFile, wszExe, sizeof(xlpFile)/sizeof(WCHAR), xlpFile, NULL))
                        cmd = xlpFile;
                    else
                        cmd = lpFile;

                    /* Add double quotation marks unless we already have them
                       (e.g.: "file://%1" %* for exefile) or unless the arg is already
                       enclosed in double quotation marks */
                    if ((res == out || *(fmt + 1) != '"') && *cmd != '"')
                    {
                        *res++ = '"';
                        strcpyW(res, cmd);
                        res += strlenW(cmd);
                        *res++ = '"';
                    }
                    else
                    {
                        strcpyW(res, cmd);
                        res += strlenW(cmd);
                    }
                }
                found_p1 = TRUE;
                break;

            /*
             * IE uses this a lot for activating things such as windows media
             * player. This is not verified to be fully correct but it appears
             * to work just fine.
             */
            case 'l':
            case 'L':
		if (lpFile) {
		    strcpyW(res, lpFile);
		    res += strlenW(lpFile);
		}
                found_p1 = TRUE;
                break;

            case 'i':
            case 'I':
		if (pidl) {
		    HGLOBAL hmem = SHAllocShared(pidl, ILGetSize(pidl), 0);
		    pv = SHLockShared(hmem, 0);
		    res += sprintfW(res, wszILPtr, pv);
		    SHUnlockShared(pv);
		}
                found_p1 = TRUE;
                break;

	    default:
                /*
                 * Check if this is an env-variable here...
                 */

                /* Make sure that we have at least one more %.*/
                if (strchrW(fmt, '%'))
                {
                    WCHAR   tmpBuffer[1024];
                    PWSTR   tmpB = tmpBuffer;
                    WCHAR   tmpEnvBuff[MAX_PATH];
                    DWORD   envRet;

                    while (*fmt != '%')
                        *tmpB++ = *fmt++;
                    *tmpB++ = 0;

                    TRACE("Checking %s to be an env-var\n", debugstr_w(tmpBuffer));

                    envRet = GetEnvironmentVariableW(tmpBuffer, tmpEnvBuff, MAX_PATH);
                    if (envRet == 0 || envRet > MAX_PATH)
                        strcpyW( res, tmpBuffer );
                    else
                        strcpyW( res, tmpEnvBuff );
                    res += strlenW(res);
                }
                done = TRUE;
                break;
            }
            /* Don't skip past terminator (catch a single '%' at the end) */
            if (*fmt != '\0')
            {
                fmt++;
            }
        }
        else
            *res++ = *fmt++;
    }

    *res = '\0';

    return found_p1;
}

HRESULT SHELL_GetPathFromIDListForExecuteA(LPCITEMIDLIST pidl, LPSTR pszPath, UINT uOutSize)
{
    STRRET strret;
    IShellFolder* desktop;

    HRESULT hr = SHGetDesktopFolder(&desktop);

    if (SUCCEEDED(hr)) {
	hr = IShellFolder_GetDisplayNameOf(desktop, pidl, SHGDN_FORPARSING, &strret);

	if (SUCCEEDED(hr))
	    StrRetToStrNA(pszPath, uOutSize, &strret, pidl);

	IShellFolder_Release(desktop);
    }

    return hr;
}

HRESULT SHELL_GetPathFromIDListForExecuteW(LPCITEMIDLIST pidl, LPWSTR pszPath, UINT uOutSize)
{
    STRRET strret;
    IShellFolder* desktop;

    HRESULT hr = SHGetDesktopFolder(&desktop);

    if (SUCCEEDED(hr)) {
	hr = IShellFolder_GetDisplayNameOf(desktop, pidl, SHGDN_FORPARSING, &strret);

	if (SUCCEEDED(hr))
	    StrRetToStrNW(pszPath, uOutSize, &strret, pidl);

	IShellFolder_Release(desktop);
    }

    return hr;
}

/*************************************************************************
 *	SHELL_ResolveShortCutW [Internal]
 *	read shortcut file at 'wcmd'
 */
static HRESULT SHELL_ResolveShortCutW(LPWSTR wcmd, LPWSTR wargs, LPWSTR wdir, HWND hwnd, LPCWSTR lpVerb, int* pshowcmd, LPITEMIDLIST* ppidl)
{
    IShellFolder* psf;

    HRESULT hr = SHGetDesktopFolder(&psf);

    *ppidl = NULL;

    if (SUCCEEDED(hr)) {
	LPITEMIDLIST pidl;
	ULONG l;

    	hr = IShellFolder_ParseDisplayName(psf, 0, 0, wcmd, &l, &pidl, 0);

	if (SUCCEEDED(hr)) {
	    IShellLinkW* psl;

	    hr = IShellFolder_GetUIObjectOf(psf, NULL, 1, (LPCITEMIDLIST*)&pidl, &IID_IShellLinkW, NULL, (LPVOID*)&psl);

	    if (SUCCEEDED(hr)) {
		hr = IShellLinkW_Resolve(psl, hwnd, 0);

		if (SUCCEEDED(hr)) {
		    hr = IShellLinkW_GetPath(psl, wcmd, MAX_PATH, NULL, SLGP_UNCPRIORITY);

		    if (SUCCEEDED(hr)) {
			if (!*wcmd) {
			    /* We could not translate the PIDL in the shell link into a valid file system path - so return the PIDL instead. */
			    hr = IShellLinkW_GetIDList(psl, ppidl);

			    if (SUCCEEDED(hr) && *ppidl) {
				/* We got a PIDL instead of a file system path - try to translate it. */
				if (SHGetPathFromIDListW(*ppidl, wcmd)) {
				    SHFree(*ppidl);
				    *ppidl = NULL;
				}
			    }
			}

			if (SUCCEEDED(hr)) {
			    /* get command line arguments, working directory and display mode if available */
			    IShellLinkW_GetWorkingDirectory(psl, wdir, MAX_PATH);
			    IShellLinkW_GetArguments(psl, wargs, MAX_PATH);
			    IShellLinkW_GetShowCmd(psl, pshowcmd);
			}
		    }
		}

		IShellLinkW_Release(psl);
	    }

	    SHFree(pidl);
	}

	IShellFolder_Release(psf);
    }

    return hr;
}

/*************************************************************************
 *	SHELL_ExecuteW [Internal]
 *
 */
static UINT_PTR SHELL_ExecuteW(const WCHAR *lpCmd, WCHAR *env, BOOL shWait,
			    LPSHELLEXECUTEINFOW psei, LPSHELLEXECUTEINFOW psei_out)
{
    STARTUPINFOW  startup;
    PROCESS_INFORMATION info;
    UINT_PTR retval = 31;
    UINT gcdret = 0;
    WCHAR curdir[MAX_PATH];

    TRACE("Execute %s from directory %s\n", debugstr_w(lpCmd), debugstr_w(psei->lpDirectory));
    /* ShellExecute specifies the command from psei->lpDirectory
     * if present. Not from the current dir as CreateProcess does */
    if( psei->lpDirectory && psei->lpDirectory[0] )
        if( ( gcdret = GetCurrentDirectoryW( MAX_PATH, curdir)))
            if( !SetCurrentDirectoryW( psei->lpDirectory))
                ERR("cannot set directory %s\n", debugstr_w(psei->lpDirectory));
    ZeroMemory(&startup,sizeof(STARTUPINFOW));
    startup.cb = sizeof(STARTUPINFOW);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = psei->nShow;
    if (CreateProcessW(NULL, (LPWSTR)lpCmd, NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT,
                       env, *psei->lpDirectory? psei->lpDirectory: NULL, &startup, &info))
    {
        /* Give 30 seconds to the app to come up, if desired. Probably only needed
           when starting app immediately before making a DDE connection. */
        if (shWait)
            if (WaitForInputIdle( info.hProcess, 30000 ) == WAIT_FAILED)
                WARN("WaitForInputIdle failed: Error %ld\n", GetLastError() );
        retval = 33;
        if (psei->fMask & SEE_MASK_NOCLOSEPROCESS)
            psei_out->hProcess = info.hProcess;
        else
            CloseHandle( info.hProcess );
        CloseHandle( info.hThread );
    }
    else if ((retval = GetLastError()) >= 32)
    {
        TRACE("CreateProcess returned error %d\n", retval);
        retval = ERROR_BAD_FORMAT;
    }

    TRACE("returning %u\n", retval);

    psei_out->hInstApp = (HINSTANCE)retval;
    if( gcdret )
        if( !SetCurrentDirectoryW( curdir))
            ERR("cannot return to directory %s\n", debugstr_w(curdir));

    return retval;
}


/***********************************************************************
 *           SHELL_BuildEnvW	[Internal]
 *
 * Build the environment for the new process, adding the specified
 * path to the PATH variable. Returned pointer must be freed by caller.
 */
static void *SHELL_BuildEnvW( const WCHAR *path )
{
    static const WCHAR wPath[] = {'P','A','T','H','=',0};
    WCHAR *strings, *new_env;
    WCHAR *p, *p2;
    int total = strlenW(path) + 1;
    BOOL got_path = FALSE;

    if (!(strings = GetEnvironmentStringsW())) return NULL;
    p = strings;
    while (*p)
    {
        int len = strlenW(p) + 1;
        if (!strncmpiW( p, wPath, 5 )) got_path = TRUE;
        total += len;
        p += len;
    }
    if (!got_path) total += 5;  /* we need to create PATH */
    total++;  /* terminating null */

    if (!(new_env = HeapAlloc( GetProcessHeap(), 0, total * sizeof(WCHAR) )))
    {
        FreeEnvironmentStringsW( strings );
        return NULL;
    }
    p = strings;
    p2 = new_env;
    while (*p)
    {
        int len = strlenW(p) + 1;
        memcpy( p2, p, len * sizeof(WCHAR) );
        if (!strncmpiW( p, wPath, 5 ))
        {
            p2[len - 1] = ';';
            strcpyW( p2 + len, path );
            p2 += strlenW(path) + 1;
        }
        p += len;
        p2 += len;
    }
    if (!got_path)
    {
        strcpyW( p2, wPath );
        strcatW( p2, path );
        p2 += strlenW(p2) + 1;
    }
    *p2 = 0;
    FreeEnvironmentStringsW( strings );
    return new_env;
}


/***********************************************************************
 *           SHELL_TryAppPathW	[Internal]
 *
 * Helper function for SHELL_FindExecutable
 * @param lpResult - pointer to a buffer of size MAX_PATH
 * On entry: szName is a filename (probably without path separators).
 * On exit: if szName found in "App Path", place full path in lpResult, and return true
 */
static BOOL SHELL_TryAppPathW( LPCWSTR szName, LPWSTR lpResult, WCHAR **env)
{
    static const WCHAR wszKeyAppPaths[] = {'S','o','f','t','w','a','r','e','\\','M','i','c','r','o','s','o','f','t','\\','W','i','n','d','o','w','s',
	'\\','C','u','r','r','e','n','t','V','e','r','s','i','o','n','\\','A','p','p',' ','P','a','t','h','s','\\',0};
    static const WCHAR wPath[] = {'P','a','t','h',0};
    HKEY hkApp = 0;
    WCHAR buffer[1024];
    LONG len;
    LONG res;
    BOOL found = FALSE;

    if (env) *env = NULL;
    strcpyW(buffer, wszKeyAppPaths);
    strcatW(buffer, szName);
    res = RegOpenKeyExW(HKEY_LOCAL_MACHINE, buffer, 0, KEY_READ, &hkApp);
    if (res) goto end;

    len = MAX_PATH*sizeof(WCHAR);
    res = RegQueryValueW(hkApp, NULL, lpResult, &len);
    if (res) goto end;
    found = TRUE;

    if (env)
    {
        DWORD count = sizeof(buffer);
        if (!RegQueryValueExW(hkApp, wPath, NULL, NULL, (LPBYTE)buffer, &count) && buffer[0])
            *env = SHELL_BuildEnvW( buffer );
    }

end:
    if (hkApp) RegCloseKey(hkApp);
    return found;
}

static UINT SHELL_FindExecutableByOperation(LPCWSTR lpPath, LPCWSTR lpFile, LPCWSTR lpOperation, LPWSTR key, LPWSTR filetype, LPWSTR command, LONG commandlen)
{
    static const WCHAR wCommand[] = {'\\','c','o','m','m','a','n','d',0};

    /* Looking for ...buffer\shell\<verb>\command */
    strcatW(filetype, wszShell);
    strcatW(filetype, lpOperation);
    strcatW(filetype, wCommand);

    if (RegQueryValueW(HKEY_CLASSES_ROOT, filetype, command,
                       &commandlen) == ERROR_SUCCESS)
    {
	commandlen /= sizeof(WCHAR);
        if (key) strcpyW(key, filetype);
#if 0
        LPWSTR tmp;
        WCHAR param[256];
	LONG paramlen = sizeof(param);
        static const WCHAR wSpace[] = {' ',0};

        /* FIXME: it seems all Windows version don't behave the same here.
         * the doc states that this ddeexec information can be found after
         * the exec names.
         * on Win98, it doesn't appear, but I think it does on Win2k
         */
	/* Get the parameters needed by the application
	   from the associated ddeexec key */
	tmp = strstrW(filetype, wCommand);
	tmp[0] = '\0';
	strcatW(filetype, wDdeexec);
	if (RegQueryValueW(HKEY_CLASSES_ROOT, filetype, param,
				     &paramlen) == ERROR_SUCCESS)
	{
	    paramlen /= sizeof(WCHAR);
            strcatW(command, wSpace);
            strcatW(command, param);
            commandlen += paramlen;
	}
#endif

	command[commandlen] = '\0';

	return 33; /* FIXME see SHELL_FindExecutable() */
    }

    return 31;	/* default - 'No association was found' */
}

/*************************************************************************
 *	SHELL_FindExecutable [Internal]
 *
 * Utility for code sharing between FindExecutable and ShellExecute
 * in:
 *      lpFile the name of a file
 *      lpOperation the operation on it (open)
 * out:
 *      lpResult a buffer, big enough :-(, to store the command to do the
 *              operation on the file
 *      key a buffer, big enough, to get the key name to do actually the
 *              command (it'll be used afterwards for more information
 *              on the operation)
 */
UINT SHELL_FindExecutable(LPCWSTR lpPath, LPCWSTR lpFile, LPCWSTR lpOperation,
                                 LPWSTR lpResult, int resultLen, LPWSTR key, WCHAR **env, LPITEMIDLIST pidl, LPCWSTR args)
{
    static const WCHAR wWindows[] = {'w','i','n','d','o','w','s',0};
    static const WCHAR wPrograms[] = {'p','r','o','g','r','a','m','s',0};
    static const WCHAR wExtensions[] = {'e','x','e',' ','p','i','f',' ','b','a','t',' ','c','m','d',' ','c','o','m',0};
    WCHAR *extension = NULL; /* pointer to file extension */
    WCHAR filetype[256];     /* registry name for this filetype */
    LONG  filetypelen = sizeof(filetype); /* length of above */
    WCHAR command[1024];     /* command from registry */
    WCHAR wBuffer[256];      /* Used to GetProfileString */
    UINT  retval = 31;       /* default - 'No association was found' */
    WCHAR *tok;              /* token pointer */
    WCHAR xlpFile[256];      /* result of SearchPath */
    DWORD attribs;           /* file attributes */

    TRACE("%s\n", (lpFile != NULL) ? debugstr_w(lpFile) : "-");

    xlpFile[0] = '\0';
    lpResult[0] = '\0'; /* Start off with an empty return string */
    if (key) *key = '\0';

    /* trap NULL parameters on entry */
    if ((lpFile == NULL) || (lpResult == NULL) || (lpOperation == NULL))
    {
        WARN("(lpFile=%s,lpResult=%s,lpOperation=%s): NULL parameter\n",
             debugstr_w(lpFile), debugstr_w(lpOperation), debugstr_w(lpResult));
        return 2; /* File not found. Close enough, I guess. */
    }

    if (SHELL_TryAppPathW( lpFile, lpResult, env ))
    {
        TRACE("found %s via App Paths\n", debugstr_w(lpResult));
        return 33;
    }

    if (SearchPathW(lpPath, lpFile, wszExe, sizeof(xlpFile)/sizeof(WCHAR), xlpFile, NULL))
    {
        TRACE("SearchPathW returned non-zero\n");
        lpFile = xlpFile;
        /* Hey, isn't this value ignored?  Why make this call?  Shouldn't we return here?  --dank*/
    }

    attribs = GetFileAttributesW(lpFile);
    if (attribs!=INVALID_FILE_ATTRIBUTES && (attribs&FILE_ATTRIBUTE_DIRECTORY))
    {
       strcpyW(filetype, wszFolder);
       filetypelen = 6;    /* strlen("Folder") */
    }
    else
    {
        /* First thing we need is the file's extension */
        extension = strrchrW(xlpFile, '.'); /* Assume last "." is the one; */
        /* File->Run in progman uses */
        /* .\FILE.EXE :( */
        TRACE("xlpFile=%s,extension=%s\n", debugstr_w(xlpFile), debugstr_w(extension));

        if (extension == NULL || extension[1]==0)
        {
            WARN("Returning 31 - No association\n");
            return 31; /* no association */
        }

        /* Three places to check: */
        /* 1. win.ini, [windows], programs (NB no leading '.') */
        /* 2. Registry, HKEY_CLASS_ROOT\<filetype>\shell\open\command */
        /* 3. win.ini, [extensions], extension (NB no leading '.' */
        /* All I know of the order is that registry is checked before */
        /* extensions; however, it'd make sense to check the programs */
        /* section first, so that's what happens here. */

        /* See if it's a program - if GetProfileString fails, we skip this
         * section. Actually, if GetProfileString fails, we've probably
         * got a lot more to worry about than running a program... */
        if (GetProfileStringW(wWindows, wPrograms, wExtensions, wBuffer, sizeof(wBuffer)/sizeof(WCHAR)) > 0)
        {
            CharLowerW(wBuffer);
            tok = wBuffer;
            while (*tok)
            {
                WCHAR *p = tok;
                while (*p && *p != ' ' && *p != '\t') p++;
                if (*p)
                {
                    *p++ = 0;
                    while (*p == ' ' || *p == '\t') p++;
                }

                if (strcmpiW(tok, &extension[1]) == 0) /* have to skip the leading "." */
                {
                    strcpyW(lpResult, xlpFile);
                    /* Need to perhaps check that the file has a path
                     * attached */
                    TRACE("found %s\n", debugstr_w(lpResult));
                    return 33;

                    /* Greater than 32 to indicate success FIXME According to the
                     * docs, I should be returning a handle for the
                     * executable. Does this mean I'm supposed to open the
                     * executable file or something? More RTFM, I guess... */
                }
                tok = p;
            }
        }

        /* Check registry */
        if (RegQueryValueW(HKEY_CLASSES_ROOT, extension, filetype,
                           &filetypelen) == ERROR_SUCCESS)
        {
            filetypelen /= sizeof(WCHAR);
            filetype[filetypelen] = '\0';
            TRACE("File type: %s\n", debugstr_w(filetype));
        }
    }

    if (*filetype)
    {
	if (lpOperation)
	{
	    /* pass the operation string to SHELL_FindExecutableByOperation() */
	    filetype[filetypelen] = '\0';
	    retval = SHELL_FindExecutableByOperation(lpPath, lpFile, lpOperation, key, filetype, command, sizeof(command));
	}
	else
	{
	    WCHAR operation[MAX_PATH];
	    HKEY hkey;

	    /* Looking for ...buffer\shell\<operation>\command */
	    strcatW(filetype, wszShell);

	    /* enumerate the operation subkeys in the registry and search for one with an associated command */
	    if (RegOpenKeyW(HKEY_CLASSES_ROOT, filetype, &hkey) == ERROR_SUCCESS)
	    {
		int idx = 0;
		for(;; ++idx)
		{
		    if (RegEnumKeyW(hkey, idx, operation, MAX_PATH) != ERROR_SUCCESS)
			break;

		    filetype[filetypelen] = '\0';
		    retval = SHELL_FindExecutableByOperation(lpPath, lpFile, operation, key, filetype, command, sizeof(command));

		    if (retval > 32)
			break;
	    }
		RegCloseKey(hkey);
	    }
	}

	if (retval > 32)
	{
	    SHELL_ArgifyW(lpResult, resultLen, command, xlpFile, pidl, args);

	    /* Remove double quotation marks and command line arguments */
	    if (*lpResult == '"')
	    {
		WCHAR *p = lpResult;
		while (*(p + 1) != '"')
		{
		    *p = *(p + 1);
		    p++;
		}
		*p = '\0';
	    }
	}
    }
    else /* Check win.ini */
    {
	static const WCHAR wExtensions[] = {'e','x','t','e','n','s','i','o','n','s',0};

	/* Toss the leading dot */
	extension++;
	if (GetProfileStringW(wExtensions, extension, wszEmpty, command, sizeof(command)/sizeof(WCHAR)) > 0)
        {
            if (strlenW(command) != 0)
            {
                strcpyW(lpResult, command);
                tok = strchrW(lpResult, '^'); /* should be ^.extension? */
                if (tok != NULL)
                {
                    tok[0] = '\0';
                    strcatW(lpResult, xlpFile); /* what if no dir in xlpFile? */
                    tok = strchrW(command, '^'); /* see above */
                    if ((tok != NULL) && (strlenW(tok)>5))
                    {
                        strcatW(lpResult, &tok[5]);
                    }
                }
                retval = 33; /* FIXME - see above */
            }
        }
    }

    TRACE("returning %s\n", debugstr_w(lpResult));
    return retval;
}

/******************************************************************
 *		dde_cb
 *
 * callback for the DDE connection. not really useful
 */
static HDDEDATA CALLBACK dde_cb(UINT uType, UINT uFmt, HCONV hConv,
                                HSZ hsz1, HSZ hsz2, HDDEDATA hData,
                                ULONG_PTR dwData1, ULONG_PTR dwData2)
{
    TRACE("dde_cb: %04x, %04x, %p, %p, %p, %p, %08lx, %08lx\n",
           uType, uFmt, hConv, hsz1, hsz2, hData, dwData1, dwData2);
    return NULL;
}

/******************************************************************
 *		dde_connect
 *
 * ShellExecute helper. Used to do an operation with a DDE connection
 *
 * Handles both the direct connection (try #1), and if it fails,
 * launching an application and trying (#2) to connect to it
 *
 */
static unsigned dde_connect(WCHAR* key, WCHAR* start, WCHAR* ddeexec,
                            const WCHAR* lpFile, WCHAR *env,
			    LPCWSTR szCommandline, LPITEMIDLIST pidl, SHELL_ExecuteW32 execfunc,
			    LPSHELLEXECUTEINFOW psei, LPSHELLEXECUTEINFOW psei_out)
{
    static const WCHAR wApplication[] = {'\\','a','p','p','l','i','c','a','t','i','o','n',0};
    static const WCHAR wTopic[] = {'\\','t','o','p','i','c',0};
    WCHAR *     endkey = key + strlenW(key);
    WCHAR       app[256], topic[256], ifexec[256], res[256];
    LONG        applen, topiclen, ifexeclen;
    WCHAR *     exec;
    DWORD       ddeInst = 0;
    DWORD       tid;
    HSZ         hszApp, hszTopic;
    HCONV       hConv;
    HDDEDATA    hDdeData;
    unsigned    ret = 31;

    strcpyW(endkey, wApplication);
    applen = sizeof(app);
    if (RegQueryValueW(HKEY_CLASSES_ROOT, key, app, &applen) != ERROR_SUCCESS)
    {
        FIXME("default app name NIY %s\n", debugstr_w(key));
        return 2;
    }

    strcpyW(endkey, wTopic);
    topiclen = sizeof(topic);
    if (RegQueryValueW(HKEY_CLASSES_ROOT, key, topic, &topiclen) != ERROR_SUCCESS)
    {
        static const WCHAR wSystem[] = {'S','y','s','t','e','m',0};
        strcpyW(topic, wSystem);
    }

    if (DdeInitializeW(&ddeInst, dde_cb, APPCMD_CLIENTONLY, 0L) != DMLERR_NO_ERROR)
    {
        return 2;
    }

    hszApp = DdeCreateStringHandleW(ddeInst, app, CP_WINUNICODE);
    hszTopic = DdeCreateStringHandleW(ddeInst, topic, CP_WINUNICODE);

    hConv = DdeConnect(ddeInst, hszApp, hszTopic, NULL);
    exec = ddeexec;
    if (!hConv)
    {
        static const WCHAR wIfexec[] = {'\\','i','f','e','x','e','c',0};
        TRACE("Launching '%s'\n", debugstr_w(start));
        ret = execfunc(start, env, TRUE, psei, psei_out);
        if (ret < 32)
        {
            TRACE("Couldn't launch\n");
            goto error;
        }
        hConv = DdeConnect(ddeInst, hszApp, hszTopic, NULL);
        if (!hConv)
        {
            TRACE("Couldn't connect. ret=%d\n", ret);
            DdeUninitialize(ddeInst);
            SetLastError(ERROR_DDE_FAIL);
            return 30; /* whatever */
        }
        strcpyW(endkey, wIfexec);
        ifexeclen = sizeof(ifexec);
        if (RegQueryValueW(HKEY_CLASSES_ROOT, key, ifexec, &ifexeclen) == ERROR_SUCCESS)
        {
            exec = ifexec;
        }
    }

    SHELL_ArgifyW(res, sizeof(res)/sizeof(WCHAR), exec, lpFile, pidl, szCommandline);
    TRACE("%s %s => %s\n", debugstr_w(exec), debugstr_w(lpFile), debugstr_w(res));

    /* It's documented in the KB 330337 that IE has a bug and returns
     * error DMLERR_NOTPROCESSED on XTYP_EXECUTE request.
     */
    hDdeData = DdeClientTransaction((LPBYTE)res, (strlenW(res) + 1) * sizeof(WCHAR), hConv, 0L, 0,
                                     XTYP_EXECUTE, 10000, &tid);
    if (hDdeData)
        DdeFreeDataHandle(hDdeData);
    else
        WARN("DdeClientTransaction failed with error %04x\n", DdeGetLastError(ddeInst));
    ret = 33;

    DdeDisconnect(hConv);

 error:
    DdeUninitialize(ddeInst);

    return ret;
}

/*************************************************************************
 *	execute_from_key [Internal]
 */
static UINT_PTR execute_from_key(LPWSTR key, LPCWSTR lpFile, WCHAR *env, LPCWSTR szCommandline,
			     SHELL_ExecuteW32 execfunc,
			     LPSHELLEXECUTEINFOW psei, LPSHELLEXECUTEINFOW psei_out)
{
    WCHAR cmd[1024];
    LONG cmdlen = sizeof(cmd);
    UINT_PTR retval = 31;

    cmd[0] = '\0';

    /* Get the application for the registry */
    if (RegQueryValueW(HKEY_CLASSES_ROOT, key, cmd, &cmdlen) == ERROR_SUCCESS)
    {
	static const WCHAR wCommand[] = {'c','o','m','m','a','n','d',0};
	static const WCHAR wDdeexec[] = {'d','d','e','e','x','e','c',0};
        LPWSTR tmp;
        WCHAR param[256];
        LONG paramlen = sizeof(param);

        param[0] = '\0';

        /* Get the parameters needed by the application
           from the associated ddeexec key */
        tmp = strstrW(key, wCommand);
        assert(tmp);
        strcpyW(tmp, wDdeexec);

        if (RegQueryValueW(HKEY_CLASSES_ROOT, key, param, &paramlen) == ERROR_SUCCESS)
        {
            TRACE("Got ddeexec %s => %s\n", debugstr_w(key), debugstr_w(param));
            retval = dde_connect(key, cmd, param, lpFile, env, szCommandline, psei->lpIDList, execfunc, psei, psei_out);
        }
        else
        {
            /* Is there a replace() function anywhere? */
            cmdlen /= sizeof(WCHAR);
            cmd[cmdlen] = '\0';
            SHELL_ArgifyW(param, sizeof(param)/sizeof(WCHAR), cmd, lpFile, psei->lpIDList, szCommandline);
            retval = execfunc(param, env, FALSE, psei, psei_out);
        }
    }
    else TRACE("ooch\n");

    return retval;
}

/*************************************************************************
 * FindExecutableA			[SHELL32.@]
 */
HINSTANCE WINAPI FindExecutableA(LPCSTR lpFile, LPCSTR lpDirectory, LPSTR lpResult)
{
    HINSTANCE retval;
    WCHAR *wFile = NULL, *wDirectory = NULL;
    WCHAR wResult[MAX_PATH];

    if (lpFile) __SHCloneStrAtoW(&wFile, lpFile);
    if (lpDirectory) __SHCloneStrAtoW(&wDirectory, lpDirectory);

    retval = FindExecutableW(wFile, wDirectory, wResult);
    WideCharToMultiByte(CP_ACP, 0, wResult, -1, lpResult, MAX_PATH, NULL, NULL);
    if (wFile) SHFree( wFile );
    if (wDirectory) SHFree( wDirectory );

    TRACE("returning %s\n", lpResult);
    return retval;
}

/*************************************************************************
 * FindExecutableW			[SHELL32.@]
 */
HINSTANCE WINAPI FindExecutableW(LPCWSTR lpFile, LPCWSTR lpDirectory, LPWSTR lpResult)
{
    UINT_PTR retval = 31;    /* default - 'No association was found' */
    WCHAR old_dir[1024];

    TRACE("File %s, Dir %s\n",
          (lpFile != NULL ? debugstr_w(lpFile) : "-"), (lpDirectory != NULL ? debugstr_w(lpDirectory) : "-"));

    lpResult[0] = '\0'; /* Start off with an empty return string */

    /* trap NULL parameters on entry */
    if ((lpFile == NULL) || (lpResult == NULL))
    {
        /* FIXME - should throw a warning, perhaps! */
	return (HINSTANCE)2; /* File not found. Close enough, I guess. */
    }

    if (lpDirectory)
    {
        GetCurrentDirectoryW(sizeof(old_dir)/sizeof(WCHAR), old_dir);
        SetCurrentDirectoryW(lpDirectory);
    }

    retval = SHELL_FindExecutable(lpDirectory, lpFile, wszOpen, lpResult, MAX_PATH, NULL, NULL, NULL, NULL);

    TRACE("returning %s\n", debugstr_w(lpResult));
    if (lpDirectory)
        SetCurrentDirectoryW(old_dir);
    return (HINSTANCE)retval;
}

/*************************************************************************
 *	ShellExecuteExW32 [Internal]
 */
BOOL WINAPI ShellExecuteExW32 (LPSHELLEXECUTEINFOW sei, SHELL_ExecuteW32 execfunc)
{
    static const WCHAR wQuote[] = {'"',0};
    static const WCHAR wSpace[] = {' ',0};
    static const WCHAR wWww[] = {'w','w','w',0};
    static const WCHAR wFile[] = {'f','i','l','e',0};
    static const WCHAR wHttp[] = {'h','t','t','p',':','/','/',0};
    static const WCHAR wExtLnk[] = {'.','l','n','k',0};
    static const WCHAR wExplorer[] = {'e','x','p','l','o','r','e','r','.','e','x','e',0};
    static const DWORD unsupportedFlags =
        SEE_MASK_INVOKEIDLIST  | SEE_MASK_ICON         | SEE_MASK_HOTKEY |
        SEE_MASK_CONNECTNETDRV | SEE_MASK_FLAG_DDEWAIT | SEE_MASK_FLAG_NO_UI |
        SEE_MASK_UNICODE       | SEE_MASK_NO_CONSOLE   | SEE_MASK_ASYNCOK |
        SEE_MASK_HMONITOR;

    WCHAR wszApplicationName[MAX_PATH+2], wszParameters[1024], wszDir[MAX_PATH];
    SHELLEXECUTEINFOW sei_tmp;	/* modifiable copy of SHELLEXECUTEINFO struct */
    WCHAR wfileName[MAX_PATH];
    WCHAR *env;
    WCHAR lpstrProtocol[256];
    LPCWSTR lpFile;
    UINT_PTR retval = 31;
    WCHAR wcmd[1024];
    WCHAR buffer[MAX_PATH];
    const WCHAR* ext;
    BOOL done;

    /* make a local copy of the LPSHELLEXECUTEINFO structure and work with this from now on */
    memcpy(&sei_tmp, sei, sizeof(sei_tmp));

    TRACE("mask=0x%08lx hwnd=%p verb=%s file=%s parm=%s dir=%s show=0x%08x class=%s\n",
            sei_tmp.fMask, sei_tmp.hwnd, debugstr_w(sei_tmp.lpVerb),
            debugstr_w(sei_tmp.lpFile), debugstr_w(sei_tmp.lpParameters),
            debugstr_w(sei_tmp.lpDirectory), sei_tmp.nShow,
            ((sei_tmp.fMask & SEE_MASK_CLASSALL) == SEE_MASK_CLASSNAME) ?
                debugstr_w(sei_tmp.lpClass) : "not used");

    sei->hProcess = NULL;

    /* make copies of all path/command strings */
    if (!sei_tmp.lpFile)
        *wszApplicationName = '\0';
    else if (*sei_tmp.lpFile == '\"')
    {
        UINT l;
        strcpyW(wszApplicationName, sei_tmp.lpFile+1);
        l=lstrlenW(wszApplicationName);
        if (wszApplicationName[l-1] == '\"')
            wszApplicationName[l-1] = '\0';
        TRACE("wszApplicationName=%s\n",debugstr_w(wszApplicationName));
    }
    else
        strcpyW(wszApplicationName, sei_tmp.lpFile);

    if (sei_tmp.lpParameters)
	strcpyW(wszParameters, sei_tmp.lpParameters);
    else
	*wszParameters = '\0';

    if (sei_tmp.lpDirectory)
	strcpyW(wszDir, sei_tmp.lpDirectory);
    else
	*wszDir = '\0';

    /* adjust string pointers to point to the new buffers */
    sei_tmp.lpFile = wszApplicationName;
    sei_tmp.lpParameters = wszParameters;
    sei_tmp.lpDirectory = wszDir;

    if (sei_tmp.fMask & unsupportedFlags)
    {
        FIXME("flags ignored: 0x%08lx\n", sei_tmp.fMask & unsupportedFlags);
    }

    /* process the IDList */
    if (sei_tmp.fMask & SEE_MASK_IDLIST)
    {
	IShellExecuteHookW* pSEH;

	HRESULT hr = SHBindToParent(sei_tmp.lpIDList, &IID_IShellExecuteHookW, (LPVOID*)&pSEH, NULL);

	if (SUCCEEDED(hr))
	{
	    hr = IShellExecuteHookW_Execute(pSEH, sei);

	    IShellExecuteHookW_Release(pSEH);

	    if (hr == S_OK)
		return TRUE;
	}

        wszApplicationName[0] = '"';
        SHGetPathFromIDListW(sei_tmp.lpIDList, wszApplicationName+1);
        strcatW(wszApplicationName, wQuote);
        TRACE("-- idlist=%p (%s)\n", sei_tmp.lpIDList, debugstr_w(wszApplicationName));
    }

    if (sei_tmp.fMask & SEE_MASK_CLASSALL)
    {
	/* launch a document by fileclass like 'WordPad.Document.1' */
        /* the Commandline contains 'c:\Path\wordpad.exe "%1"' */
        /* FIXME: szCommandline should not be of a fixed size. Fixed to 1024, MAX_PATH is way too short! */
        ULONG cmask=(sei_tmp.fMask & SEE_MASK_CLASSALL);
        HCR_GetExecuteCommandW((cmask == SEE_MASK_CLASSKEY) ? sei_tmp.hkeyClass : NULL,
                               (cmask == SEE_MASK_CLASSNAME) ? sei_tmp.lpClass: NULL,
                               (sei_tmp.lpVerb) ? sei_tmp.lpVerb : wszOpen,
                               wszParameters, sizeof(wszParameters)/sizeof(WCHAR));

        /* FIXME: get the extension of lpFile, check if it fits to the lpClass */
        TRACE("SEE_MASK_CLASSNAME->'%s', doc->'%s'\n", debugstr_w(wszParameters), debugstr_w(wszApplicationName));

        wcmd[0] = '\0';
        done = SHELL_ArgifyW(wcmd, sizeof(wcmd)/sizeof(WCHAR), wszParameters, wszApplicationName, sei_tmp.lpIDList, NULL);
        if (!done && wszApplicationName[0])
        {
            strcatW(wcmd, wSpace);
            strcatW(wcmd, wszApplicationName);
        }
        retval = execfunc(wcmd, NULL, FALSE, &sei_tmp, sei);
        if (retval > 32)
            return TRUE;
        else
            return FALSE;
    }


    /* resolve shell shortcuts */
    ext = PathFindExtensionW(sei_tmp.lpFile);

    if (ext && !strncmpiW(ext, wExtLnk, sizeof(wExtLnk) / sizeof(WCHAR) - 1) &&
        (ext[sizeof(wExtLnk) / sizeof(WCHAR) - 1] == '\0' ||
         (sei_tmp.lpFile[0] == '"' && ext[sizeof(wExtLnk) / sizeof(WCHAR) - 1] == '"')))	/* or check for: shell_attribs & SFGAO_LINK */
    {
	HRESULT hr;
	BOOL Quoted;

	if (wszApplicationName[0] == '"')
	{
	    if (wszApplicationName[strlenW(wszApplicationName) - 1] == '"')
	    {
		wszApplicationName[strlenW(wszApplicationName) - 1] = '\0';
		Quoted = TRUE;
	    }
	    else
	    {
		Quoted = FALSE;
	    }
	}
	else
	{
	    Quoted = FALSE;
	}
	/* expand paths before reading shell link */
	if (ExpandEnvironmentStringsW(Quoted ? sei_tmp.lpFile + 1 : sei_tmp.lpFile, buffer, MAX_PATH))
	    lstrcpyW(Quoted ? wszApplicationName + 1 : wszApplicationName/*sei_tmp.lpFile*/, buffer);

	if (*sei_tmp.lpParameters)
	    if (ExpandEnvironmentStringsW(sei_tmp.lpParameters, buffer, MAX_PATH))
		lstrcpyW(wszParameters/*sei_tmp.lpParameters*/, buffer);

	hr = SHELL_ResolveShortCutW((LPWSTR)(Quoted ? sei_tmp.lpFile + 1 : sei_tmp.lpFile),
	                            (LPWSTR)sei_tmp.lpParameters, (LPWSTR)sei_tmp.lpDirectory,
				    sei_tmp.hwnd, sei_tmp.lpVerb?sei_tmp.lpVerb:wszEmpty, &sei_tmp.nShow, (LPITEMIDLIST*)&sei_tmp.lpIDList);
	if (Quoted)
	{
	    wszApplicationName[strlenW(wszApplicationName) + 1] = '\0';
	    wszApplicationName[strlenW(wszApplicationName)] = '"';
	}

	if (sei->lpIDList)
	    sei->fMask |= SEE_MASK_IDLIST;

	if (SUCCEEDED(hr))
	{
	    /* repeat IDList processing if needed */
	    if (sei_tmp.fMask & SEE_MASK_IDLIST)
	    {
		IShellExecuteHookW* pSEH;

		HRESULT hr = SHBindToParent(sei_tmp.lpIDList, &IID_IShellExecuteHookW, (LPVOID*)&pSEH, NULL);

		if (SUCCEEDED(hr))
		{
		    hr = IShellExecuteHookW_Execute(pSEH, sei);

		    IShellExecuteHookW_Release(pSEH);

		    if (hr == S_OK)
			return TRUE;
		}

		TRACE("-- idlist=%p (%s)\n", debugstr_w(sei_tmp.lpIDList), debugstr_w(sei_tmp.lpFile));
	    }
	}
    }


    /* Has the IDList not yet been translated? */
    if (sei_tmp.fMask & SEE_MASK_IDLIST)
    {
	/* last chance to translate IDList: now also allow CLSID paths */
	if (SUCCEEDED(SHELL_GetPathFromIDListForExecuteW(sei_tmp.lpIDList, buffer, sizeof(buffer)))) {
	    if (buffer[0]==':' && buffer[1]==':') {
		/* open shell folder for the specified class GUID */
		strcpyW(wszParameters, buffer);
		strcpyW(wszApplicationName, wExplorer);

		sei_tmp.fMask &= ~SEE_MASK_INVOKEIDLIST;
	    } else if (HCR_GetExecuteCommandW(0, wszFolder, sei_tmp.lpVerb?sei_tmp.lpVerb:wszOpen, buffer, sizeof(buffer))) {
		SHELL_ArgifyW(wszApplicationName, sizeof(wszApplicationName)/sizeof(WCHAR), buffer, NULL, sei_tmp.lpIDList, NULL);

		sei_tmp.fMask &= ~SEE_MASK_INVOKEIDLIST;
	    }
	}
    }

    /* expand environment strings */
    if (ExpandEnvironmentStringsW(sei_tmp.lpFile, buffer, MAX_PATH))
	lstrcpyW(wszApplicationName, buffer);

    if (*sei_tmp.lpParameters)
        if (ExpandEnvironmentStringsW(sei_tmp.lpParameters, buffer, MAX_PATH))
	    lstrcpyW(wszParameters, buffer);

    if (*sei_tmp.lpDirectory)
	if (ExpandEnvironmentStringsW(sei_tmp.lpDirectory, buffer, MAX_PATH))
	    lstrcpyW(wszDir, buffer);

    /* Else, try to execute the filename */
    TRACE("execute:%s,%s,%s\n", debugstr_w(wszApplicationName), debugstr_w(wszParameters), debugstr_w(wszDir));

    /* separate out command line arguments from executable file name */
    if (!*sei_tmp.lpParameters) {
	/* If the executable path is quoted, handle the rest of the command line as parameters. */
	if (sei_tmp.lpFile[0] == '"') {
	    LPWSTR src = wszApplicationName/*sei_tmp.lpFile*/ + 1;
	    LPWSTR dst = wfileName;
	    LPWSTR end;

	    /* copy the unquoted executable path to 'wfileName' */
	    while(*src && *src!='"')
		*dst++ = *src++;

	    *dst = '\0';

	    if (*src == '"') {
		end = ++src;

		while(isspace(*src))
		    ++src;
	    } else
		end = src;

	    /* copy the parameter string to 'wszParameters' */
	    strcpyW(wszParameters, src);

	    /* terminate previous command string after the quote character */
	    *end = '\0';
	}
	else
	{
	    /* If the executable name is not quoted, we have to use this search loop here,
	       that in CreateProcess() is not sufficient because it does not handle shell links. */
	    WCHAR buffer[MAX_PATH], xlpFile[MAX_PATH];
	    LPWSTR space, s;

	    LPWSTR beg = wszApplicationName/*sei_tmp.lpFile*/;
	    for(s=beg; (space=strchrW(s, ' ')); s=space+1) {
		int idx = space-sei_tmp.lpFile;
		memcpy(buffer, sei_tmp.lpFile, idx * sizeof(WCHAR));
		buffer[idx] = '\0';

		/*FIXME This finds directory paths if the targeted file name contains spaces. */
		if (SearchPathW(*sei_tmp.lpDirectory? sei_tmp.lpDirectory: NULL, buffer, wszExe, sizeof(xlpFile), xlpFile, NULL))
		{
		    /* separate out command from parameter string */
		    LPCWSTR p = space + 1;

		    while(isspaceW(*p))
			++p;

		    strcpyW(wszParameters, p);
		    *space = '\0';

		    break;
		}
	    }

	    strcpyW(wfileName, sei_tmp.lpFile);
	}
    } else
	strcpyW(wfileName, sei_tmp.lpFile);

    lpFile = wfileName;

    if (sei_tmp.lpParameters[0]) {
        strcatW(wszApplicationName, wSpace);
        strcatW(wszApplicationName, wszParameters);
    }

    /* We set the default to open, and that should generally work.
       But that is not really the way the MS docs say to do it. */
    if (!sei_tmp.lpVerb)
        sei_tmp.lpVerb = wszOpen;

    retval = execfunc(wszApplicationName, NULL, FALSE, &sei_tmp, sei);
    if (retval > 32)
        return TRUE;

    /* Else, try to find the executable */
    wcmd[0] = '\0';
    retval = SHELL_FindExecutable(sei_tmp.lpDirectory, lpFile, sei_tmp.lpVerb, wcmd, 1024, lpstrProtocol, &env, sei_tmp.lpIDList, sei_tmp.lpParameters);
    if (retval > 32)  /* Found */
    {
        WCHAR wszQuotedCmd[MAX_PATH+2];
        /* Must quote to handle case where cmd contains spaces,
         * else security hole if malicious user creates executable file "C:\\Program"
         */
        strcpyW(wszQuotedCmd, wQuote);
        strcatW(wszQuotedCmd, wcmd);
        strcatW(wszQuotedCmd, wQuote);
        if (wszParameters[0]) {
            strcatW(wszQuotedCmd, wSpace);
            strcatW(wszQuotedCmd, wszParameters);
        }
        TRACE("%s/%s => %s/%s\n", debugstr_w(wszApplicationName), debugstr_w(sei_tmp.lpVerb), debugstr_w(wszQuotedCmd), debugstr_w(lpstrProtocol));
        if (*lpstrProtocol)
            retval = execute_from_key(lpstrProtocol, wszApplicationName, env, sei_tmp.lpParameters, execfunc, &sei_tmp, sei);
        else
            retval = execfunc(wszQuotedCmd, env, FALSE, &sei_tmp, sei);
        HeapFree( GetProcessHeap(), 0, env );
    }
    else if (PathIsURLW((LPWSTR)lpFile))    /* File not found, check for URL */
    {
	static const WCHAR wShell[] = {'\\','s','h','e','l','l','\\',0};
	static const WCHAR wCommand[] = {'\\','c','o','m','m','a','n','d',0};
        LPWSTR lpstrRes;
        INT iSize;

        lpstrRes = strchrW(lpFile, ':');
        if (lpstrRes)
            iSize = lpstrRes - lpFile;
        else
            iSize = strlenW(lpFile);

        TRACE("Got URL: %s\n", debugstr_w(lpFile));
        /* Looking for ...protocol\shell\lpOperation\command */
        memcpy(lpstrProtocol, lpFile, iSize*sizeof(WCHAR));
        lpstrProtocol[iSize] = '\0';
        strcatW(lpstrProtocol, wShell);
        strcatW(lpstrProtocol, sei_tmp.lpVerb? sei_tmp.lpVerb: wszOpen);
        strcatW(lpstrProtocol, wCommand);

        /* Remove File Protocol from lpFile */
        /* In the case file://path/file     */
        if (!strncmpiW(lpFile, wFile, iSize))
        {
            lpFile += iSize;
            while (*lpFile == ':') lpFile++;
        }
        retval = execute_from_key(lpstrProtocol, lpFile, NULL, sei_tmp.lpParameters, execfunc, &sei_tmp, sei);
    }
    /* Check if file specified is in the form www.??????.*** */
    else if (!strncmpiW(lpFile, wWww, 3))
    {
        /* if so, append lpFile http:// and call ShellExecute */
        WCHAR lpstrTmpFile[256];
        strcpyW(lpstrTmpFile, wHttp);
        strcatW(lpstrTmpFile, lpFile);
        retval = (UINT_PTR)ShellExecuteW(sei_tmp.hwnd, sei_tmp.lpVerb, lpstrTmpFile, NULL, NULL, 0);
    }

    TRACE("retval %u\n", retval);

    if (retval <= 32)
    {
        sei->hInstApp = (HINSTANCE)retval;
        return FALSE;
    }

    sei->hInstApp = (HINSTANCE)33;
    return TRUE;
}

/*************************************************************************
 * ShellExecuteA			[SHELL32.290]
 */
HINSTANCE WINAPI ShellExecuteA(HWND hWnd, LPCSTR lpOperation,LPCSTR lpFile,
                               LPCSTR lpParameters,LPCSTR lpDirectory, INT iShowCmd)
{
    SHELLEXECUTEINFOA sei;
    HANDLE hProcess = 0;

    TRACE("%p,%s,%s,%s,%s,%d\n",
          hWnd, debugstr_a(lpOperation), debugstr_a(lpFile),
          debugstr_a(lpParameters), debugstr_a(lpDirectory), iShowCmd);

    sei.cbSize = sizeof(sei);
    sei.fMask = 0;
    sei.hwnd = hWnd;
    sei.lpVerb = lpOperation;
    sei.lpFile = lpFile;
    sei.lpParameters = lpParameters;
    sei.lpDirectory = lpDirectory;
    sei.nShow = iShowCmd;
    sei.lpIDList = 0;
    sei.lpClass = 0;
    sei.hkeyClass = 0;
    sei.dwHotKey = 0;
    sei.hProcess = hProcess;

    ShellExecuteExA (&sei);
    return sei.hInstApp;
}

/*************************************************************************
 * ShellExecuteExA				[SHELL32.292]
 *
 */
BOOL WINAPI ShellExecuteExA (LPSHELLEXECUTEINFOA sei)
{
    SHELLEXECUTEINFOW seiW;
    BOOL ret;
    WCHAR *wVerb = NULL, *wFile = NULL, *wParameters = NULL, *wDirectory = NULL, *wClass = NULL;

    TRACE("%p\n", sei);

    memcpy(&seiW, sei, sizeof(SHELLEXECUTEINFOW));

    if (sei->lpVerb)
	seiW.lpVerb = __SHCloneStrAtoW(&wVerb, sei->lpVerb);

    if (sei->lpFile)
        seiW.lpFile = __SHCloneStrAtoW(&wFile, sei->lpFile);

    if (sei->lpParameters)
        seiW.lpParameters = __SHCloneStrAtoW(&wParameters, sei->lpParameters);

    if (sei->lpDirectory)
        seiW.lpDirectory = __SHCloneStrAtoW(&wDirectory, sei->lpDirectory);

    if ((sei->fMask & SEE_MASK_CLASSALL) == SEE_MASK_CLASSNAME && sei->lpClass)
        seiW.lpClass = __SHCloneStrAtoW(&wClass, sei->lpClass);
    else
        seiW.lpClass = NULL;

    ret = ShellExecuteExW32 (&seiW, SHELL_ExecuteW);

    sei->hInstApp = seiW.hInstApp;

    if (sei->fMask & SEE_MASK_NOCLOSEPROCESS)
        sei->hProcess = seiW.hProcess;

    if (wVerb) SHFree(wVerb);
    if (wFile) SHFree(wFile);
    if (wParameters) SHFree(wParameters);
    if (wDirectory) SHFree(wDirectory);
    if (wClass) SHFree(wClass);

    return ret;
}

/*************************************************************************
 * ShellExecuteExW				[SHELL32.293]
 *
 */
BOOL WINAPI ShellExecuteExW (LPSHELLEXECUTEINFOW sei)
{
    return  ShellExecuteExW32 (sei, SHELL_ExecuteW);
}

/*************************************************************************
 * ShellExecuteW			[SHELL32.294]
 * from shellapi.h
 * WINSHELLAPI HINSTANCE APIENTRY ShellExecuteW(HWND hwnd, LPCWSTR lpOperation,
 * LPCWSTR lpFile, LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd);
 */
HINSTANCE WINAPI ShellExecuteW(HWND hwnd, LPCWSTR lpOperation, LPCWSTR lpFile,
                               LPCWSTR lpParameters, LPCWSTR lpDirectory, INT nShowCmd)
{
    SHELLEXECUTEINFOW sei;
    HANDLE hProcess = 0;

    TRACE("\n");
    sei.cbSize = sizeof(sei);
    sei.fMask = 0;
    sei.hwnd = hwnd;
    sei.lpVerb = lpOperation;
    sei.lpFile = lpFile;
    sei.lpParameters = lpParameters;
    sei.lpDirectory = lpDirectory;
    sei.nShow = nShowCmd;
    sei.lpIDList = 0;
    sei.lpClass = 0;
    sei.hkeyClass = 0;
    sei.dwHotKey = 0;
    sei.hProcess = hProcess;

    ShellExecuteExW32 (&sei, SHELL_ExecuteW);
    return sei.hInstApp;
}
