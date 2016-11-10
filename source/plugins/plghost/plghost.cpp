#include "stdafx.h"

#pragma USELIB("ipc")
#pragma USELIB("plgcommon")

#pragma comment(lib, "shared.lib")

#pragma comment(lib, "Winmm.lib")
#pragma comment(lib, "Msacm32.lib")
#pragma comment(lib, "Shlwapi.lib")
//#pragma comment(lib, "Ws2_32.lib")
//#pragma comment(lib, "comctl32.lib")

#if defined _FINAL || defined _DEBUG_OPTIMIZED
#include "crt_nomem/crtfunc.h"
#endif

#pragma warning (push)
#pragma warning (disable:4324)
#include "libsodium/src/libsodium/include/sodium.h"
#pragma warning (pop)

ipc::ipc_junction_s *ipcj = nullptr;
static bool panica = false;

static void __stdcall operation_result(long_operation_e op, int rslt);
static void __stdcall update_contact(const contact_data_s *);
static void __stdcall message(message_type_e mt, int gid, int cid, u64 create_time, const char *msgbody_utf8, int mlen);
static void __stdcall delivered(u64 utag);
static void __stdcall save();
static void __stdcall on_save(const void *data, int dlen, void *param);
static void __stdcall export_data(const void *data, int dlen);
static void __stdcall av_data(int gid, int cid, const media_data_s *data);
static void __stdcall free_video_data(const void *ptr);
static void __stdcall av_stream_options(int gid, int cid, const stream_options_s *so);
static void __stdcall configurable(int n, const char **fields, const char **values);
static void __stdcall avatar_data(int cid, int tag, const void *avatar_body, int avatar_body_size);
static void __stdcall incoming_file(int cid, u64 utag, u64 filesize, const char *filename_utf8, int filenamelen);
static bool __stdcall file_portion(u64 utag, u64 offset, const void *portion, int portion_size);
static void __stdcall file_control(u64 utag, file_control_e fctl);
static void __stdcall typing(int gid, int cid);
static void __stdcall telemetry( telemetry_e k, const void *data, int datasize );

static void fix_pf( proto_info_s &pi )
{
    if ( pi.features & PF_UNAUTHORIZED_CONTACT )
        pi.features |= PF_UNAUTHORIZED_CHAT;
}

struct protolib_s
{
    host_functions_s hostfunctions;
    HMODULE protolib;
    proto_functions_s *functions;

    cmd_result_e load(const wchar_t *protolibname, proto_info_s& pi)
    {
        protolib = LoadLibraryW(protolibname);
        if (protolib)
        {
            getinfo_pf gi = (getinfo_pf)GetProcAddress(protolib,"api_getinfo");
            if (!gi) return CR_FUNCTION_NOT_FOUND;

            gi(&pi);
            fix_pf( pi );

            handshake_pf handshake = (handshake_pf)GetProcAddress(protolib, "api_handshake");
            if (handshake)
            {
                functions = handshake( &hostfunctions );
                functions->logging_flags(g_logging_flags);
                functions->telemetry_flags( g_telemetry_flags );

            } else
            {
                FreeLibrary(protolib);
                protolib = nullptr;
                return CR_FUNCTION_NOT_FOUND;
            }
       
            return CR_OK;
        }
        return CR_MODULE_NOT_FOUND;
    }

    bool loaded() const
    {
        return protolib != nullptr;
    }

    ~protolib_s()
    {
        if (protolib)
        {
            functions->goodbye();
            FreeLibrary(protolib);
        }
    }

} protolib = 
{
    {
        operation_result,
        update_contact,
        message,
        delivered,
        save,
        on_save,
        export_data,
        av_data,
        free_video_data,
        av_stream_options,
        configurable,
        avatar_data,
        incoming_file,
        file_portion,
        file_control,
        typing,
        telemetry,
    },
    nullptr, // protolib
    nullptr, // functions

};

namespace
{
    struct data_data_s
    {
        int sz;
        data_header_s d;
        data_data_s():d(NOP_COMMAND) {} // nobody call this constructor
    
        ipcr get_reader() const
        {
            ASSERT(sz > sizeof(data_header_s));
            return ipcr(&d,sz);
        }

        static data_data_s *build(int szsz)
        {
            data_data_s *me = (data_data_s*)ph_allocator::ma(sizeof(data_data_s) - sizeof(data_header_s) + szsz);
            me->sz = szsz;
            return me;
        }

        static data_data_s *build( data_header_s *dd, int szsz )
        {
            if (szsz < 0)
            {
                data_data_s *me = (data_data_s *)(((char *)dd) - (sizeof(data_data_s) - sizeof(data_header_s)));
                me->sz = szsz;
                return me;
            }

            if (szsz == sizeof(data_header_s))
            {
                data_data_s *me = (data_data_s*)ph_allocator::ma(sizeof(data_data_s));
                me->sz = 0;
                me->d = *dd;
                return me;
            } else
            {
                data_data_s *me = (data_data_s*)ph_allocator::ma(sizeof(data_data_s) - sizeof(data_header_s) + szsz);
                me->sz = szsz;
                memcpy(&me->d, dd, szsz);
                return me;
            }
        }
        void die()
        {
            if (sz < 0)
                ipcj->unlock_buffer( &d );
            else
                ph_allocator::mf(this);
        }
    };

    static_assert(sizeof(data_data_s) - sizeof(data_header_s) <= ipc::XCHG_BUFFER_ADDITION_SPACE, "size error");

}

struct ipcwbuf_s
{
    struct ipcwspace : public ipcw
    {
        ipcwspace *next = nullptr;
        ipcwspace *prev = nullptr;
    };
    static const int preallocated_bitems = 64;
    ipcwspace data[preallocated_bitems];
    ipcwspace *first = nullptr;
    ipcwspace *last = nullptr;

    ipcwspace *first_a = nullptr;
    ipcwspace *last_a = nullptr;

    ipcwbuf_s()
    {
        for(int i=0;i<preallocated_bitems;++i)
            LIST_ADD( (data+i), first, last, prev, next ); //-V619 // Wat? PVS-Studio too paranoiac?
    }
    ~ipcwbuf_s()
    {
        while (first_a)
        {
            ipcwspace *spc = first_a;
            LIST_DEL(spc, first_a, last_a, prev, next);
            spc->~ipcwspace();
            ph_allocator::mf(spc);
        }
    }

    ipcw *get(commands_e cmd)
    {
        if (first)
        {
            if (first_a)
            {
                ipcwspace *spc = first_a;
                LIST_DEL(spc, first_a, last_a, prev, next );
                spc->~ipcwspace();
                ph_allocator::mf(spc);
            }

            ipcwspace *spc = first;
            LIST_DEL(spc, first, last, prev, next );
            spc->clear(cmd);
            return spc;
        }
        if (first_a)
        {
            ipcwspace *spc = first_a;
            LIST_DEL(spc, first_a, last_a, prev, next);
            spc->clear(cmd);
            return spc;
        }
        ipcwspace *spc = (ipcwspace *)ph_allocator::ma(sizeof(ipcwspace));
        spc->ipcwspace::ipcwspace();
        spc->clear(cmd);
        return spc;
    }

    void kill(ipcw *w)
    {
        if (w >= (data+0) && w < (data + preallocated_bitems))
        {
            LIST_ADD((ipcwspace *)w, first, last, prev, next );
        } else
        {
            LIST_ADD((ipcwspace *)w, first_a, last_a, prev, next);
        }
    }
};

spinlock::syncvar<ipcwbuf_s> ipcwbuf;

spinlock::spinlock_queue_s<data_data_s *, ph_allocator> tasks;
spinlock::spinlock_queue_s<ipcw *, ph_allocator> sendbufs;

struct state_s
{
    int working;
    bool need_stop;
    state_s():working(0), need_stop(false) {}
};

spinlock::syncvar<state_s> state;

struct data_block_s
{
    const void *data;
    int datasize;
    data_block_s(const void *data, int datasize):data(data), datasize(datasize) {}
};

struct IPCW
{
    ipcw *w;
    IPCW(commands_e cmd)
    {
        w = ipcwbuf.lock_write()().get(cmd);
    }
    ~IPCW()
    {
        if (w) sendbufs.push(w);
    }

    template<typename T> struct put2buf { static void put( ipcw *w, const T &v ) { w->add<T>() = v; } };
    template<> struct put2buf<asptr> { static void put( ipcw *w, const asptr &s ) { w->add(s); } };
    template<> struct put2buf<wsptr> { static void put( ipcw *w, const wsptr &s ) { w->add(s); } };
    template<> struct put2buf< std::vector<char, ph_allocator> >
    {
        static void put( ipcw *w, const std::vector<char, ph_allocator> &v )
        {
            w->add<i32>() = (i32)v.size();
            if (v.size()) w->add( v.data(), v.size() );
        }
    };
    template<> struct put2buf<data_block_s>
    {
        static void put( ipcw *w, const data_block_s &d)
        {
            w->add<i32>() = (i32)d.datasize;
            if (d.datasize) w->add( d.data, d.datasize );
        }
    };

    template<size_t hashsize> struct put2buf< blake2b<hashsize> >
    {
        static void put( ipcw *w, const blake2b<hashsize> &b )
        {
            w->add( b.hash, hashsize );
        }
    };

    template<typename T> IPCW & operator<<(const T &v) { if (w) put2buf<T>::put(w,v); return *this; }

};

namespace
{
    struct bigdata_s
    {
        data_data_s *stord = nullptr;
        data_data_s *curdd = nullptr;
        int disp = 0;
        ~bigdata_s()
        {
            ASSERT(curdd == nullptr);
            if (stord && stord->d.cmd == 0)
                stord->die();
        }
    };
}

ipc::ipc_result_e event_processor( void *dptr, void *data, int datasize )
{
    if (panica)
        return ipc::IPCR_BREAK;

    bigdata_s *bd = (bigdata_s *)dptr;

    if (data == nullptr)
    {
        if ( bd->curdd )
        {
            // big data received
            ASSERT( datasize == 0 );
            tasks.push( bd->curdd );
            bd->curdd = nullptr;
            return ipc::IPCR_OK;
        }
        // big data

        if (bd->stord && bd->stord->d.cmd > 0)
            return ipc::IPCR_SKIP;

        if (bd->stord == nullptr || datasize > bd->stord->sz)
        {
            if (bd->stord)
                bd->stord->die();
            bd->stord = data_data_s::build(datasize);
        }
        bd->curdd = bd->stord;
        bd->disp = 0;
        return ipc::IPCR_OK;
    }

    if (bd->curdd)
    {
        // big data
        if (datasize == 0)
        return ipc::IPCR_BREAK;

        ASSERT( (bd->disp + datasize) <= bd->curdd->sz );

        char *ptr = (char *)&bd->curdd->d;
        ptr += bd->disp;
        memcpy( ptr, data, datasize );
        bd->disp += datasize;
        return ipc::IPCR_OK;
    }

    data_header_s *d = (data_header_s *)data;
    switch (d->cmd)
    {
    case AQ_VERSION:
        IPCW(HA_VERSION) << PLGHOST_IPC_PROTOCOL_VERSION;
        break;

    default:
        tasks.push(data_data_s::build(d, datasize));
        break;
    }
    return ipc::IPCR_OK;
}

unsigned long exec_task( data_data_s *d, unsigned long flags );

DELTA_TIME_PROFILER(x1, 1024);

DWORD WINAPI worker(LPVOID nonzerothread)
{
    UNSTABLE_CODE_PROLOG

    data_data_s* d = nullptr;
    ipcw *w;

    ++state.lock_write()().working;

    int sleepvalue = nonzerothread ? 1 : 10;
    for(;!state.lock_read()().need_stop; )
    {
        DELTA_TIME_CHECKPOINT( x1 );

        if (protolib.loaded() && !nonzerothread) // nonzerothread: tick is single-threaded, so i can be called only in one thread
            protolib.functions->tick(&sleepvalue);
        
        if (sleepvalue < 0)
        {
            panica = true;
            ipcj->stop_signal();
            break;
        }

        DELTA_TIME_CHECKPOINT( x1 );

        unsigned long flags = 0;
        while (tasks.try_pop(d))
            flags = exec_task(d, flags);

        DELTA_TIME_CHECKPOINT( x1 );

        while (sendbufs.try_pop(w))
        {
             ipcj->send(w->data(), (int)w->size());
             ipcwbuf.lock_write()().kill(w);
        }

        DELTA_TIME_CHECKPOINT( x1 );

        if (sleepvalue >= 0)
            Sleep(sleepvalue);

        DELTA_TIME_CHECKPOINT( x1 );

    }

    --state.lock_write()().working;

    UNSTABLE_CODE_EPILOG
    return 0;
}

int CALLBACK WinMainProtect(
    _In_  LPSTR lpCmdLine
    )
{
    if ( !lpCmdLine[0] )
    {
        MessageBoxA( nullptr, "usage: plghost <ipc token>", "", MB_OK );
        return 0;
    }

    int num_workers = 1; // TODO : set to number of processor cores minus 1 for better performance

    for(int i=0;i<num_workers;++i)
    {
        CloseHandle(CreateThread(nullptr, 0, worker, (LPVOID)(size_t)i, 0, nullptr));
    }

    ipc::ipc_junction_s ipcblob;
    ipcj = &ipcblob;
    int member = ipcblob.start(lpCmdLine);

    if (member == 1)
    {
        bigdata_s bd;
        ipcblob.wait(event_processor, &bd, nullptr, nullptr);
    }
    state.lock_write()().need_stop = true;
    while (state.lock_read()().working) Sleep(0); // worker must die

    if (member == 1) ipcblob.stop();
    ipcj = nullptr;

    data_data_s* d = nullptr;

    while (tasks.try_pop(d))
        d->die();

    ipcw *w;
    while (sendbufs.try_pop(w))
        ipcwbuf.lock_write()().kill(w);

    return 0;
}

int CALLBACK WinMain(
    _In_  HINSTANCE hInstance,
    _In_  HINSTANCE,
    _In_  LPSTR lpCmdLine,
    _In_  int
    )
{
    g_module = hInstance;
#if defined _DEBUG || defined _CRASH_HANDLER
#include "../appver.inl"
    exception_operator_c::set_unhandled_exception_filter();
    exception_operator_c::dump_filename = fn_change_name_ext(get_exe_full_name(), wstr_c(CONSTWSTR("plghost")).append_char('.').appendcvt(SS(APPVERD)).as_sptr(), 
#ifdef MODE64
        CONSTWSTR( "x64.dmp" ) );
#else
        CONSTWSTR( "dmp" ) );
#endif // MODE64                   
#endif

    UNSTABLE_CODE_PROLOG
        WinMainProtect(lpCmdLine);
    UNSTABLE_CODE_EPILOG
        return 0;
}

wstr_c mypath()
{
    wstr_c path;
    path.set_length(2048 - 8);
    int len = GetModuleFileNameW(nullptr, path.str(), 2048 - 8);
    path.set_length(len);

    if (path.get_char(0) == '\"')
    {
        int s = path.find_pos(1, '\"');
        path.set_length(s);
    }
    path.trim_right();
    path.trim_right('\"');
    path.trim_left('\"');

    return path;

}

inline bool check_loaded(int cmd, bool f)
{
    if (!f)
    {
        IPCW(HA_CMD_STATUS) << cmd << (int)CR_MODULE_NOT_FOUND;
    }

    return f;
}

#define LIBLOADED() check_loaded(d->d.cmd, protolib.loaded())

unsigned long exec_task(data_data_s *d, unsigned long flags)
{
#define FLAG_SAVE_CONFIG 1
#define FLAG_SAVE_CONFIG_MULTI 2

    switch (d->d.cmd)
    {
    case AQ_GET_PROTOCOLS_LIST:
        {
            IPCW w(HA_PROTOCOLS_LIST);
            auto cnt = w.w->reserve<int>();

            wstr_c path = mypath();
            int truncp = path.find_last_pos_of(CONSTWSTR("/\\"));
            if (truncp>0)
            {
                path.set_length(truncp+1).append(CONSTWSTR("proto.*.dll"));

            
                WIN32_FIND_DATAW find_data;
                HANDLE           fh = FindFirstFileW(path, &find_data);

                while (fh != INVALID_HANDLE_VALUE)
                {
                    path.set_length(truncp+1).append( find_data.cFileName );
                    HMODULE l = LoadLibraryW(path);
                    if ( getinfo_pf f = (getinfo_pf)GetProcAddress( l, "api_getinfo" ) )
                    {

                        proto_info_s info;
                        f( &info );
                        fix_pf( info );

                        int numofstrings = 0;
                        for ( ; info.strings && info.strings[ numofstrings ]; ) ++numofstrings;

                        w << info.features;
                        w << info.connection_features;

                        w << numofstrings;
                        for ( int i = 0; i < numofstrings; ++i )
                            w << asptr( info.strings[ i ] );

                        ++cnt;
                    }

                    FreeLibrary( l );
                    if (!FindNextFileW(fh, &find_data)) break;
                }

                if (fh != INVALID_HANDLE_VALUE) FindClose(fh);
            }
        }
        break;
    case AQ_SET_PROTO:
        {
            ipcr r(d->get_reader());
            tmp_str_c proto = r.getastr();
            tmp_wstr_c path = mypath();
            proto_info_s pi;

            int truncp = path.find_last_pos_of(CONSTWSTR("/\\"));
            cmd_result_e rst = CR_MODULE_NOT_FOUND;
            if (truncp > 0)
            {
                path.set_length(truncp + 1).append(CONSTWSTR("proto.")).appendcvt(proto).append(CONSTWSTR(".dll"));

                WIN32_FIND_DATAW find_data;
                HANDLE fh = FindFirstFileW(path, &find_data);

                if (fh != INVALID_HANDLE_VALUE)
                {
                    rst = protolib.load(path, pi);
#if defined _DEBUG || defined _CRASH_HANDLER
                    if (CR_OK == rst)
                    {
                        exception_operator_c::dump_filename.replace_all(CONSTWSTR(".dmp"), wstr_c(CONSTWSTR(".")).appendcvt(proto)
#ifdef MODE64
                            .append( CONSTWSTR( ".x64.dmp" ) ) );
#else
                            .append(CONSTWSTR(".dmp")));
#endif // MODE64                   
                    }
#endif
                    FindClose(fh);
                }
            }

            int numofstrings = 0;
            for ( ; pi.strings && pi.strings[ numofstrings ]; ) ++numofstrings;

            IPCW status( HA_CMD_STATUS );
                status
                << (int)AQ_SET_PROTO << (int)rst << pi.priority << pi.indicator << pi.features << pi.connection_features << numofstrings;

                for ( int i = 0; i < numofstrings; ++i )
                    status << asptr( pi.strings[ i ] );
        }
        break;
    case AQ_SET_NAME:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            tmp_str_c name = r.getastr();
            protolib.functions->set_name(name);
        }
        break;
    case AQ_SET_STATUSMSG:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            tmp_str_c status = r.getastr();
            protolib.functions->set_statusmsg(status);
        }
        break;
    case AQ_SET_CONFIG:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int sz;
            if (const void *data = r.get_data(sz))
                protolib.functions->set_config(data,sz);
        }
        break;
    case AQ_SET_AVATAR:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int sz;
            if (const void *data = r.get_data(sz))
                protolib.functions->set_avatar(data, sz);
        }
        break;
    case AQ_INIT_DONE:
        if (LIBLOADED())
        {
            protolib.functions->init_done();
        }
        break;
    case AQ_ONLINE:
        if (LIBLOADED())
        {
            protolib.functions->online();
        }
        break;
    case AQ_OFFLINE:
        if (LIBLOADED())
        {
            protolib.functions->offline();
        }
        break;
    case AQ_OSTATE:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int ost = r.get<int>();
            protolib.functions->set_ostate(ost);
        }
        break;
    case AQ_JOIN_CONFERENCE:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int gid = r.get<int>();
            int cid = r.get<int>();
            protolib.functions->join_conference(gid, cid);
        }
        break;
    case AQ_REN_CONFERENCE:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int gid = r.get<int>();
            tmp_str_c gchname = r.getastr();
            protolib.functions->ren_conference(gid, gchname);
        }
        break;
    case AQ_CREATE_CONFERENCE:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            tmp_str_c confaname = r.getastr();
            tmp_str_c confao = r.getastr();
            protolib.functions->create_conference( confaname, confao );
        }
        break;
    case AQ_DEL_CONFERENCE:
        if (LIBLOADED())
        {
            ipcr r( d->get_reader() );
            tmp_str_c confaid = r.getastr();
            protolib.functions->del_conference( confaid );
        }
        break;
    case AQ_ENTER_CONFERENCE:
        if ( LIBLOADED() )
        {
            ipcr r( d->get_reader() );
            tmp_str_c confaid = r.getastr();
            protolib.functions->enter_conference( confaid );
        }
        break;
    case AQ_LEAVE_CONFERENCE:
        if ( LIBLOADED() )
        {
            ipcr r( d->get_reader() );
            int gid = r.get<int>();
            int options = r.get<int>();
            protolib.functions->leave_conference( gid, options );
        }
        break;
    case AQ_ADD_CONTACT:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int rslt = 0;
            switch ( r.get<char>() )
            {
            case 0:
                {
                    tmp_str_c publicid = r.getastr();
                    tmp_str_c invitemsg = r.getastr();
                    rslt = protolib.functions->add_contact( publicid, invitemsg );
                }
                break;
            case 1:
                {
                    // resend
                    int cid = r.get<int>();
                    tmp_str_c invitemsg = r.getastr();
                    rslt = protolib.functions->resend_request( cid, invitemsg );
                }
                break;
            case 2:
                {
                    tmp_str_c publicid = r.getastr();
                    rslt = protolib.functions->add_contact( publicid, nullptr );
                }
                break;
            }
            IPCW(HA_CMD_STATUS) << (int)AQ_ADD_CONTACT << rslt;
        }
        break;
    case AQ_DEL_CONTACT:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            protolib.functions->del_contact(id);
        }
        break;
    case AQ_MESSAGE:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            u64 utag = r.get<u64>();
            u64 crtime = r.get<u64>();
            str_c message = r.getastr();
            message_s m;
            m.utag = utag;
            m.crtime = crtime;
            m.message = message.cstr();
            m.message_len = message.get_length();
            protolib.functions->send_message(id, &m);
        }
        break;
    case AQ_SAVE_CONFIG:
        if (0 != (FLAG_SAVE_CONFIG_MULTI & flags)) break;
        if (0 != (FLAG_SAVE_CONFIG & flags))
        {
            IPCW(HA_CMD_STATUS) << (int)AQ_SAVE_CONFIG << (int)CR_MULTIPLE_CALL;
            flags |= FLAG_SAVE_CONFIG_MULTI;
            break;
        }
        flags |= FLAG_SAVE_CONFIG;
        if (LIBLOADED())
        {
            std::vector<char, ph_allocator> cfg;
            protolib.functions->save_config(&cfg);
            if (cfg.size()) IPCW(HA_CONFIG) << cfg << blake2b<crypto_generichash_BYTES_MIN>(cfg);
            else IPCW(HA_CMD_STATUS) << (int)AQ_SAVE_CONFIG << (int)CR_FUNCTION_NOT_FOUND;
        }
        break;
    case AQ_ACCEPT:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            protolib.functions->accept(id);
        }
        break;
    case AQ_REJECT:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            protolib.functions->reject(id);
        }
        break;
    case AQ_STOP_CALL:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            protolib.functions->stop_call(id);
        }
        break;
    case AQ_ACCEPT_CALL:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            protolib.functions->accept_call(id);
        }
        break;
    case AQ_SEND_AUDIO:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            call_info_s cinf;
            cinf.audio_data = r.get_data(cinf.audio_data_size);
            cinf.ms_monotonic = r.get<u64>();
            protolib.functions->send_av(id, &cinf);
        }
        break;
    case AQ_STREAM_OPTIONS:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            stream_options_s so;
            so.options = r.get<int>();
            so.view_w = r.get<int>();
            so.view_h = r.get<int>();
            protolib.functions->stream_options(id, &so);
        }
        break;
    case AQ_CALL:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            call_info_s cinf;
            cinf.duration = r.get<int>();
            protolib.functions->call(id, &cinf);
        }
        break;
    case AQ_FILE_SEND:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            file_send_info_s fi;
            int id = r.get<int>();
            fi.utag = r.get<u64>();
            fi.filename = r.readastr(fi.filename_len);
            fi.filesize = r.get<u64>();
            protolib.functions->file_send(id, &fi);
        }
        break;
    case AQ_ACCEPT_FILE:
        if ( LIBLOADED() )
        {
            ipcr r( d->get_reader() );
            u64 utag = r.get<u64>();
            u64 offset = r.get<u64>();
            protolib.functions->file_accept( utag, offset );
        }
        break;
    case AQ_CONTROL_FILE:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            u64 utag = r.get<u64>();
            file_control_e fc = (file_control_e)r.get<i32>();
            if (FIC_ACCEPT == fc)
            {
                ERROR( "use accept file" );
            } else
                protolib.functions->file_control(utag, fc);
            if (fc == FIC_BREAK || fc == FIC_REJECT || fc == FIC_DONE)
            {
                //prebuf_s::kill(utag);
            }
        }
        break;
    case AA_FILE_PORTION:
        if (LIBLOADED())
        {
            struct fd_s
            {
                u64 utag;
                u64 offset;
                int sz;
            };

            fd_s *fd = (fd_s *)(d + 1);

            file_portion_s fp;
            fp.offset = fd->offset;
            fp.data = fd+1;
            fp.size = fd->sz;
            if (protolib.functions->file_portion(fd->utag, &fp))
                return flags;
        }
        break;
    case AQ_GET_AVATAR_DATA:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            protolib.functions->get_avatar(id);
        }
        break;
    case AQ_DEL_MESSAGE:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            u64 utag = r.get<u64>();
            protolib.functions->del_message(utag);
        }
        break;
    case AQ_TYPING:
        if (LIBLOADED())
        {
            ipcr r(d->get_reader());
            int id = r.get<int>();
            protolib.functions->typing(id);
        }
        break;
    case XX_PING:
        {
            ipcr r(d->get_reader());
            int sz;
            const void *data = r.get_data(sz);
            IPCW(XX_PONG) << data_block_s(data, sz);
        }
        break;
    case AQ_VIDEO:
        {
            struct inf_s
            {
                int cid;
                int w;
                int h;
                int fmt;
                u64 msmonotonic;
                u64 padding;
            };
            static_assert( sizeof( inf_s ) == VIDEO_FRAME_HEADER_SIZE, "size!" );

            inf_s *inf = (inf_s *)(d + 1);
            call_info_s ci;
            ci.video_data = inf + 1;
            ci.w = inf->w;
            ci.h = inf->h;
            ci.fmt = (video_fmt_e)inf->fmt;
            ci.ms_monotonic = inf->msmonotonic;

            int r = protolib.functions->send_av(inf->cid, &ci);
            if (r == SEND_AV_KEEP_VIDEO_DATA)
            {
                if (CHECK(d->sz < 0, "only xchg buffers"))
                    return flags;
            }
        }

        if (d->sz > ipc::BIG_DATA_SIZE)
            return flags; // do not delete big data packet due it will be reused
        break;

    case AQ_EXPORT_DATA:
        protolib.functions->export_data();
        break;
    case AQ_DEBUG_SETTINGS:
        {
            g_logging_flags = 0;
            g_telemetry_flags = 0;
            if (protolib.functions) protolib.functions->logging_flags(0);
#if defined _DEBUG || defined _CRASH_HANDLER
            MINIDUMP_TYPE dump_type = (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithHandleData);
#endif
    
            ipcr r(d->get_reader());
            parse_values(r.getastr(), [&](const pstr_c &k, const pstr_c &v) {

                if (k.equals(CONSTASTR(DEBUG_OPT_FULL_DUMP)))
                {
#if defined _DEBUG || defined _CRASH_HANDLER
                    if (v.as_int() != 0)
                        dump_type = (MINIDUMP_TYPE)(MiniDumpWithFullMemory | MiniDumpWithDataSegs | MiniDumpWithHandleData);
#endif
                }
                else if (k.equals(CONSTASTR(DEBUG_OPT_LOGGING)))
                {
                    g_logging_flags = (unsigned)v.as_int();
                }
                else if ( k.equals(CONSTASTR(DEBUG_OPT_TELEMETRY)))
                {
                    g_telemetry_flags = (unsigned)v.as_int();
                }
            });

            if ( protolib.functions ) protolib.functions->logging_flags( g_logging_flags );
            if ( protolib.functions ) protolib.functions->telemetry_flags( g_telemetry_flags );


#if defined _DEBUG || defined _CRASH_HANDLER
            exception_operator_c::set_dump_type(dump_type);
#endif

        }
        break;
    }

    d->die();
    return flags;
}

static void __stdcall operation_result(long_operation_e op, int rslt)
{
    switch (op)
    {
    case LOP_ADDCONTACT: {
            IPCW(HA_CMD_STATUS) << (int)AQ_ADD_CONTACT << rslt;
            break;
        }
    case LOP_SETCONFIG: {
            IPCW(HA_CMD_STATUS) << (int)AQ_SET_CONFIG << rslt;
            break;
        }
    case LOP_ONLINE: {
            IPCW(HA_CMD_STATUS) << (int)AQ_ONLINE << rslt;
            break;
        }
    }
}

static void __stdcall update_contact(const contact_data_s *cd)
{
#ifdef _DEBUG
    if ( cd->id < 0 )
    {
        if ((cd->mask & CDM_PUBID) == 0)
            __debugbreak();
        if ((cd->mask & CDM_SPECIAL_BITS) == 0)
            __debugbreak();

        if (pstr_c(asptr(cd->public_id,cd->public_id_len)).begins("CC3B0"))
            if ( 0 == (cd->mask & CDF_AUDIO_CONFERENCE) && cd->state == CS_ONLINE )
                __debugbreak();

    }
#endif // _DEBUG

    IPCW ucs(HQ_UPDATE_CONTACT);
    ucs << cd->id << cd->mask
        << ((0 != (cd->mask & CDM_PUBID)) ? asptr(cd->public_id, cd->public_id_len) : asptr("",0))
        << ((0 != (cd->mask & CDM_NAME)) ? asptr(cd->name, cd->name_len) : asptr("",0))
        << ((0 != (cd->mask & CDM_STATUSMSG)) ? asptr(cd->status_message, cd->status_message_len) : asptr("",0))
        << cd->avatar_tag << static_cast<int>(cd->state) << static_cast<int>(cd->ostate) << static_cast<int>(cd->gender) << cd->conference_permissions;
    
    if ( 0 != (cd->mask & CDM_MEMBERS) )
    {
        ucs << cd->members_count;
        for(int i=0; i<cd->members_count; ++i)
            ucs << cd->members[i];
    }
    if (0 != (cd->mask & CDM_DETAILS))
    {
        ucs << asptr(cd->details, cd->details_len);
    }
}

static void __stdcall message(message_type_e mt, int gid, int cid, u64 create_time, const char *msgbody_utf8, int mlen)
{
    static u64 last_createtime = 0;
    static byte lastmessage_blake2b[crypto_generichash_BYTES] = {};
    if (mt == MT_MESSAGE && (last_createtime == 0 || (create_time - last_createtime) < 60))
    {
        blake2b<sizeof( lastmessage_blake2b )> b( msgbody_utf8, mlen );
        if (0 == memcmp( lastmessage_blake2b, b.hash, sizeof( lastmessage_blake2b ) )) return; // double
        memcpy( lastmessage_blake2b, b.hash, sizeof( lastmessage_blake2b ) );
    }
    last_createtime = create_time;
    IPCW(HQ_MESSAGE) << gid << cid << static_cast<int>(mt) << create_time << asptr(msgbody_utf8, mlen);
}

static void __stdcall save()
{
    IPCW(HQ_SAVE) << 0;
}

static void __stdcall delivered(u64 utag)
{
    IPCW(HA_DELIVERED) << utag;
}

static void __stdcall on_save(const void *data, int dlen, void *param)
{
    std::vector<char, ph_allocator> *buffer = (std::vector<char, ph_allocator> *)param;
    size_t offset = buffer->size();
    buffer->resize( buffer->size() + dlen );
    memcpy( buffer->data() + offset, data, dlen );
}

static void __stdcall av_stream_options(int gid, int cid, const stream_options_s *so)
{
    IPCW(HQ_STREAM_OPTIONS) << gid << cid << so->options << so->view_w << so->view_h;
}

static void __stdcall export_data(const void *data, int dlen)
{
    IPCW(HQ_EXPORT_DATA) << data_block_s(data, dlen);
}

static void __stdcall av_data(int gid, int cid, const media_data_s *data)
{
    if (data->audio_framesize)
    {
        IPCW(HQ_AUDIO) << gid << cid
            << data->afmt.sample_rate << data->afmt.channels << data->afmt.bits
            << data_block_s(data->audio_frame, data->audio_framesize) << data->msmonotonic;
    }

    struct inf_s
    {
        int gid;
        int cid;
        int w;
        int h;
        u64 msmonotonic;
        u64 padding;
    };

    static_assert( sizeof(inf_s) == VIDEO_FRAME_HEADER_SIZE, "size!" );

    if (data->vfmt.fmt != VFMT_NONE)
    {
        switch (data->vfmt.fmt)
        {
        case VFMT_XRGB:
            {
                int xrgb_sz = data->vfmt.width * data->vfmt.height * 4;
                int xrgbbufsz = xrgb_sz + sizeof(data_header_s) + sizeof(inf_s);
                data_header_s *dh = (data_header_s *)ipcj->lock_buffer(xrgbbufsz);

                if (!dh) return;
                inf_s *d2s = (inf_s *)(dh + 1);
                dh->cmd = HQ_VIDEO;

                d2s->gid = gid;
                d2s->cid = cid;
                d2s->w = data->vfmt.width;
                d2s->h = data->vfmt.height;
                d2s->msmonotonic = data->msmonotonic;

                byte *body = (byte *)(d2s + 1);

                if (data->vfmt.pitch[0] == data->vfmt.width * 4)
                {
                    // same pitch
                    memcpy(body, data->video_frame[0], xrgb_sz);
                }
                else
                {
                    int pitch_to = data->vfmt.width * 4;
                    int pitch_from = data->vfmt.pitch[0];
                    int tt = data->vfmt.height;
                    const byte *dfrom = (const byte *)data->video_frame[0];
                    for (int t = 0; t < tt; ++t, body += pitch_to, dfrom += pitch_from)
                        memcpy(body, dfrom, pitch_to);
                }

                ipcj->unlock_send_buffer(dh, xrgbbufsz);
            }
            break;
        case VFMT_I420:
            {
                int xrgb_sz = data->vfmt.width * data->vfmt.height * 4 + sizeof( data_header_s ) + sizeof(inf_s);
                data_header_s *dh = (data_header_s *)ipcj->lock_buffer(xrgb_sz);
                if (!dh) return;
                inf_s *d2s = (inf_s *)(dh+1);
                dh->cmd = HQ_VIDEO;

                d2s->gid = gid;
                d2s->cid = cid;
                d2s->w = data->vfmt.width;
                d2s->h = data->vfmt.height;
                d2s->msmonotonic = data->msmonotonic;

                byte *body = (byte *)(d2s + 1);

                img_helper_i420_to_ARGB((const byte *)data->video_frame[0], data->vfmt.pitch[0],
                                        (const byte *)data->video_frame[1], data->vfmt.pitch[1], 
                                        (const byte *)data->video_frame[2], data->vfmt.pitch[2], 
                                        body, data->vfmt.width * 4, data->vfmt.width, data->vfmt.height);

                ipcj->unlock_send_buffer(dh, xrgb_sz);
            }
            break;
        }
    }
}

static void __stdcall free_video_data(const void *ptr)
{
    ipcj->unlock_buffer(ptr);
}

static void __stdcall configurable(int n, const char **fields, const char **values)
{
    IPCW s(HA_CONFIGURABLE);
    s << n;
    for(int i=0;i<n;++i)
    {
        s << asptr(fields[i]);
        s << asptr(values[i]);
    }
}

static void __stdcall avatar_data(int cid, int tag, const void *avatar_body, int avatar_body_size)
{
    IPCW(HQ_AVATAR_DATA) << cid << tag << data_block_s(avatar_body, avatar_body_size);
}

static void __stdcall incoming_file(int cid, u64 utag, u64 filesize, const char *filename_utf8, int filenamelen)
{
    IPCW(HQ_INCOMING_FILE) << cid << utag << filesize << asptr(filename_utf8, filenamelen);
}

static void __stdcall file_control(u64 utag, file_control_e fctl)
{
    switch(fctl)
    {
    case FIC_DONE:
    case FIC_REJECT:
    case FIC_BREAK:
    case FIC_DISCONNECT:
        break;
    }
    IPCW(AQ_CONTROL_FILE) << utag << (int)fctl;
}

static bool __stdcall file_portion(u64 utag, u64 offset, const void *portion, int portion_size)
{
    if (portion == nullptr)
    {
        // 1
        u64 megaindex = offset >> 20;
        if (portion_size != 1048576)
        {
            Log("Bad portion request size: %i, expected 1048576", portion_size);
            return false;
        }
        IPCW(HQ_QUERY_FILE_PORTION) << utag << megaindex;
        return true;
    }

    if ( portion_size == 0 )
    {
        // 3
        if ( ipcj ) ipcj->unlock_buffer(portion);
        return true;
    }

    struct fd_s
    {
        u64 tag;
        u64 offset;
        int size;
    };

    int bsize = portion_size + sizeof(data_header_s) + sizeof(fd_s);

    data_header_s *dh = (data_header_s *)ipcj->lock_buffer(bsize);
    if (!dh) return false;
    dh->cmd = HQ_FILE_PORTION;

    fd_s *d = (fd_s *)(dh + 1);
    d->tag = utag;
    d->offset = offset;
    d->size = portion_size;

    memcpy( d + 1, portion, portion_size);
    ipcj->unlock_send_buffer( dh, bsize );
    return true;
}

static void __stdcall typing(int gid, int cid)
{
    IPCW(HQ_TYPING) << gid << cid;
}

static void __stdcall telemetry( telemetry_e k, const void *data, int datasize )
{
    IPCW( HQ_TELEMETRY ) << (int)k << data_block_s(data, datasize);
}