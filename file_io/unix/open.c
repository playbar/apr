/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000-2001 The Apache Software Foundation.  All rights
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 */

#include "fileio.h"
#include "apr_strings.h"
#include "apr_portable.h"

apr_status_t apr_unix_file_cleanup(void *thefile)
{
    apr_file_t *file = thefile;
    apr_status_t flush_rv = APR_SUCCESS, rv = APR_SUCCESS;
    int rc;

    if (file->buffered) {
        flush_rv = apr_file_flush(file);
    }
    rc = close(file->filedes);
    if (rc == 0) {
        file->filedes = -1;
#if APR_HAS_THREADS
        if (file->thlock) {
            rv = apr_lock_destroy(file->thlock);
        }
#endif
    }
    else {
	/* Are there any error conditions other than EINTR or EBADF? */
        rv = errno;
    }
    return rv != APR_SUCCESS ? rv : flush_rv;
}

apr_status_t apr_file_open(apr_file_t **new, const char *fname, apr_int32_t flag,  apr_fileperms_t perm, apr_pool_t *cont)
{
    int oflags = 0;
#if APR_HAS_THREADS
    apr_status_t rv;
#endif

    (*new) = (apr_file_t *)apr_pcalloc(cont, sizeof(apr_file_t));
    (*new)->cntxt = cont;
    (*new)->flags = flag;
    (*new)->filedes = -1;

    if ((flag & APR_READ) && (flag & APR_WRITE)) {
        oflags = O_RDWR;
    }
    else if (flag & APR_READ) {
        oflags = O_RDONLY;
    }
    else if (flag & APR_WRITE) {
        oflags = O_WRONLY;
    }
    else {
        return APR_EACCES; 
    }

    (*new)->fname = apr_pstrdup(cont, fname);

    (*new)->blocking = BLK_ON;
    (*new)->buffered = (flag & APR_BUFFERED) > 0;

    if ((*new)->buffered) {
        (*new)->buffer = apr_palloc(cont, APR_FILE_BUFSIZE);
#if APR_HAS_THREADS
        rv = apr_lock_create(&((*new)->thlock), APR_MUTEX, APR_INTRAPROCESS, 
                            NULL, cont);
        if (rv) {
            return rv;
        }
#endif
    }
    else {
        (*new)->buffer = NULL;
    }

    if (flag & APR_CREATE) {
        oflags |= O_CREAT; 
	if (flag & APR_EXCL) {
	    oflags |= O_EXCL;
	}
    }
    if ((flag & APR_EXCL) && !(flag & APR_CREATE)) {
        return APR_EACCES;
    }   

    if (flag & APR_APPEND) {
        oflags |= O_APPEND;
    }
    if (flag & APR_TRUNCATE) {
        oflags |= O_TRUNC;
    }
    
    if (perm == APR_OS_DEFAULT) {
        (*new)->filedes = open(fname, oflags, 0666);
    }
    else {
        (*new)->filedes = open(fname, oflags, apr_unix_perms2mode(perm));
    }    

    if ((*new)->filedes < 0) {
       (*new)->filedes = -1;
       (*new)->eof_hit = 1;
       return errno;
    }

    if (flag & APR_DELONCLOSE) {
        unlink(fname);
    }
    (*new)->pipe = 0;
    (*new)->timeout = -1;
    (*new)->ungetchar = -1;
    (*new)->eof_hit = 0;
    (*new)->filePtr = 0;
    (*new)->bufpos = 0;
    (*new)->dataRead = 0;
    (*new)->direction = 0;
    apr_pool_cleanup_register((*new)->cntxt, (void *)(*new), apr_unix_file_cleanup,
                              ((*new)->flag & APR_INHERIT) ? apr_pool_cleanup_null 
                                                           : apr_unix_file_cleanup);
    return APR_SUCCESS;
}

apr_status_t apr_file_close(apr_file_t *file)
{
    apr_status_t rv;

    if ((rv = apr_unix_file_cleanup(file)) == APR_SUCCESS) {
        apr_pool_cleanup_kill(file->cntxt, file, apr_unix_file_cleanup);
        return APR_SUCCESS;
    }
    return rv;
}

apr_status_t apr_file_remove(const char *path, apr_pool_t *cont)
{
    if (unlink(path) == 0) {
        return APR_SUCCESS;
    }
    else {
        return errno;
    }
}

apr_status_t apr_file_rename(const char *from_path, const char *to_path,
                           apr_pool_t *p)
{
    if (rename(from_path, to_path) != 0) {
        return errno;
    }
    return APR_SUCCESS;
}

apr_status_t apr_os_file_get(apr_os_file_t *thefile, apr_file_t *file)
{
    *thefile = file->filedes;
    return APR_SUCCESS;
}

apr_status_t apr_os_file_put(apr_file_t **file, apr_os_file_t *thefile,
                             apr_pool_t *cont)
{
    int *dafile = thefile;
    
    (*file) = apr_pcalloc(cont, sizeof(apr_file_t));
    (*file)->cntxt = cont;
    (*file)->eof_hit = 0;
    (*file)->buffered = 0;
    (*file)->blocking = BLK_UNKNOWN; /* in case it is a pipe */
    (*file)->timeout = -1;
    (*file)->ungetchar = -1; /* no char avail */
    (*file)->filedes = *dafile;
    /* buffer already NULL; 
     * don't get a lock (only for buffered files) 
     */
    (*file)->inherit = 1;
    return APR_SUCCESS;
}    

apr_status_t apr_file_eof(apr_file_t *fptr)
{
    if (fptr->eof_hit == 1) {
        return APR_EOF;
    }
    return APR_SUCCESS;
}   

apr_status_t apr_file_open_stderr(apr_file_t **thefile, apr_pool_t *cont)
{
    int fd = STDERR_FILENO;

    return apr_os_file_put(thefile, &fd, cont);
}

apr_status_t apr_file_open_stdout(apr_file_t **thefile, apr_pool_t *cont)
{
    int fd = STDOUT_FILENO;

    return apr_os_file_put(thefile, &fd, cont);
}

apr_status_t apr_file_open_stdin(apr_file_t **thefile, apr_pool_t *cont)
{
    int fd = STDIN_FILENO;

    return apr_os_file_put(thefile, &fd, cont);
}

APR_IMPLEMENT_SET_INHERIT(file, flags, cntxt, apr_unix_file_cleanup)

APR_IMPLEMENT_UNSET_INHERIT(file, flags, cntxt, apr_unix_file_cleanup)

APR_POOL_IMPLEMENT_ACCESSOR_X(file, cntxt);
