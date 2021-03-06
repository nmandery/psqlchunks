#include <cstdlib>
#include <algorithm>
#include <sstream>

#include <ctime>
#include <cstring>
#include <cerrno>

#include "debug.h"
#include "db.h"

#define CANCEL_BUF_SIZE     256

using namespace PsqlChunks;


bool
timeval_subtract (struct timeval &result, struct timeval & x, struct timeval &y)
{
    if (x.tv_usec < y.tv_usec) {
        int nsec = (y.tv_usec - x.tv_usec) / 1000000 + 1;
        y.tv_usec -= 1000000 * nsec;
        y.tv_sec += nsec;
    }
    if (x.tv_usec - y.tv_usec > 1000000) {
        int nsec = (x.tv_usec - y.tv_usec) / 1000000;
        y.tv_usec += 1000000 * nsec;
        y.tv_sec -= nsec;
    }

    result.tv_sec = x.tv_sec - y.tv_sec;
    result.tv_usec = x.tv_usec - y.tv_usec;

    return x.tv_sec < y.tv_sec;
}



Db::Db()
    : conn(NULL), do_commit(false), failed_count(0), in_transaction(false)
{
}


Db::~Db()
{
    disconnect();
}

bool
Db::connect( const char * host, const char * db_name,  const char * port, const char * user, const char * passwd)
{
    conn = PQsetdbLogin(host, port, NULL, NULL, db_name, user, passwd);
    return isConnected();
}


bool
Db::setEncoding(const char * enc_name)
{
    if (enc_name == NULL) {
        return false;
    }

    if (PQsetClientEncoding(conn, enc_name) != 0) {
        return false;
    }

    return true;
}

void
Db::disconnect()
{
    finish();
    PQfinish(conn);
}


bool
Db::isConnected()
{
    if (PQstatus(conn) != CONNECTION_OK) {
        log_debug("no db connection");
        return false;
    }
    log_debug("got a working db connection");
    return true;
}


std::string
Db::getErrorMessage()
{
    std::string msg;
    if (conn) {
        msg.assign(PQerrorMessage(conn));
    }
    return msg;
}


bool
Db::runChunk(Chunk & chunk)
{
    if (!isConnected()) {
        DbException e("lost db connection");
        throw e;
    }

    begin();

    std::string sql = chunk.getSql();
    chunk.diagnostics.status = Diagnostics::Ok;

    // start time
    struct timeval start_time;
    if (gettimeofday(&start_time, NULL)!=0) {
        DbException e(strerror(errno));
        throw e;
    }

    executeSql("savepoint chunk;");

    PGresult * pgres = PQexec(conn, sql.c_str());
    if (!pgres) {
        log_error("PQExec failed");
        DbException e("PQExec failed");
        throw e;
    }

    // end time
    struct timeval end_time;
    if (gettimeofday(&end_time, NULL)!=0) {
        DbException e(strerror(errno));
        throw e;
    }
    timeval_subtract(chunk.diagnostics.runtime, end_time, start_time);


    if ((PQresultStatus(pgres) == PGRES_FATAL_ERROR) ||
        (PQresultStatus(pgres) == PGRES_NONFATAL_ERROR)) {
        chunk.diagnostics.status = Diagnostics::Fail;

        // error line and position in that line
        char * statement_position = PQresultErrorField(pgres, PG_DIAG_STATEMENT_POSITION);
        if (statement_position) {
            int pos = atoi(statement_position);
            if ((sql.begin()+pos) < sql.end()) {
                chunk.diagnostics.error_line = chunk.start_line
                                + std::count(sql.begin(), sql.begin()+pos, '\n');
            }
            else {
                log_error("PG_DIAG_STATEMENT_POSITION is beyond the length of sql string");
            }
        }
        else {
            log_debug("got an empty PG_DIAG_STATEMENT_POSITION");
            chunk.diagnostics.error_line = LINE_NUMBER_NOT_AVAILABLE;
        }

        char * sqlstate = PQresultErrorField(pgres, PG_DIAG_SQLSTATE);
        if (sqlstate) {
            chunk.diagnostics.sqlstate.assign(sqlstate);
        }

        char * msg_primary = PQresultErrorField(pgres, PG_DIAG_MESSAGE_PRIMARY);
        if (msg_primary) {
            chunk.diagnostics.msg_primary.assign(msg_primary);
        }

        char * msg_detail = PQresultErrorField(pgres, PG_DIAG_MESSAGE_DETAIL);
        if (msg_detail) {
            chunk.diagnostics.msg_detail.assign(msg_detail);
        }

        char * msg_hint = PQresultErrorField(pgres, PG_DIAG_MESSAGE_HINT);
        if (msg_hint) {
            chunk.diagnostics.msg_hint.assign(msg_hint);
        }
    }

    PQclear(pgres);

    if (chunk.diagnostics.status != Diagnostics::Ok) {
        executeSql("rollback to savepoint chunk;");
        failed_count++;
    }
    else {
        executeSql("release savepoint chunk;");
    }

    return (chunk.diagnostics.status == Diagnostics::Ok);
}


void
Db::finish()
{
    if (failed_count>0) {
        rollback();
    }
    else {
        commit();
    }
    failed_count = 0;
}


void
Db::executeSql(const char * sqlstr, bool silent)
{
    if (!isConnected()) {
        log_warn("can not run chunk - no db connection");
    }

    log_debug("executing sql: %s", sqlstr);

    PGresult * pgres = PQexec(conn, sqlstr);
    if (!pgres) {
        log_error("PQExec failed");
        DbException e("PQExec failed");
        throw e;
    }

    if ((PQresultStatus(pgres) == PGRES_FATAL_ERROR) ||
        (PQresultStatus(pgres) == PGRES_NONFATAL_ERROR)) {
        const char * error_primary = PQresultErrorField(pgres, PG_DIAG_MESSAGE_PRIMARY);

        std::stringstream msgstream;
        std::string msg;
        msgstream << "could not execute query \"" << sqlstr << "\": "<< error_primary;
        msg = msgstream.str();

        PQclear(pgres);

        if (!silent) {
            log_error("%s", msg.c_str());
        }
        DbException e(msg);
        throw e;
    }


    PQclear(pgres);
}


void
Db::begin()
{
    if (!in_transaction) {
        executeSql("begin;");
        in_transaction = true;
    }
}


void
Db::commit()
{
    if (!do_commit) {
        return rollback();
    }

    if (in_transaction) {
        executeSql("commit;");
        in_transaction = false;
    }
}


void
Db::rollback()
{
    if (in_transaction) {
        executeSql("rollback;");
        in_transaction = false;
    }
}


bool
Db::cancel(std::string & errmsg)
{
    if (!isConnected()) {
        log_debug("not connected - no query to cancel");
        return true; // nothing to cancel
    }

    PGcancel * cncl;
    cncl = PQgetCancel(conn);
    if (cncl == NULL) {
        log_error("could not get PGCancel pointer");
        return false;
    }
    log_debug("Query successfuly canceled");

    char cnclbuf[CANCEL_BUF_SIZE];
    bool success = true;
    if (PQcancel(cncl, &cnclbuf[0], CANCEL_BUF_SIZE) != 1) {
        success = false;
        log_debug("could not cancel running query: %s", cnclbuf);
        errmsg.assign(cnclbuf);
    }

    PQfreeCancel(cncl);
    return success;
}
