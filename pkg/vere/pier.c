/// @file

#include "db/lmdb.h"
#include "ent/ent.h"
#include "noun.h"
#include "pace.h"
#include "vere.h"
#include "version.h"
#include "curl/curl.h"

#define PIER_READ_BATCH 1000ULL
#define PIER_PLAY_BATCH 500ULL
#define PIER_WORK_BATCH 10ULL

#undef VERBOSE_PIER

/* _pier_peek_plan(): add a u3_pico to the peek queue
*/
static void
_pier_peek_plan(u3_pier* pir_u, u3_pico* pic_u)
{
  if (!pir_u->pec_u.ent_u) {
    u3_assert( !pir_u->pec_u.ext_u );
    pir_u->pec_u.ent_u = pir_u->pec_u.ext_u = pic_u;
  }
  else {
    pir_u->pec_u.ent_u->nex_u = pic_u;
    pir_u->pec_u.ent_u = pic_u;
  }

  u3_pier_spin(pir_u);
}

/* _pier_peek_next(): pop u3_pico off of peek queue
*/
static u3_pico*
_pier_peek_next(u3_pier* pir_u)
{
  u3_pico* pic_u = pir_u->pec_u.ext_u;

  if (pic_u) {
    pir_u->pec_u.ext_u = pic_u->nex_u;
    if (!pir_u->pec_u.ext_u) {
      pir_u->pec_u.ent_u = 0;
    }

    pic_u->nex_u = 0;
  }

  return pic_u;
}

/* _pier_work_send(): send new events for processing
*/
static void
_pier_work_send(u3_work* wok_u)
{
  u3_auto* car_u = wok_u->car_u;
  u3_pier* pir_u = wok_u->pir_u;
  u3_lord* god_u = pir_u->god_u;
  c3_w     len_w = 0;

  //  calculate work batch size
  {
    u3_wall* wal_u = wok_u->wal_u;

    if ( !wal_u ) {
      //  XX work depth, or full lord send-stack depth?
      //
      if ( PIER_WORK_BATCH > god_u->dep_w ) {
        len_w = PIER_WORK_BATCH - god_u->dep_w;
      }
    }
    else {
      c3_d sen_d = god_u->eve_d + god_u->dep_w;
      if ( wal_u->eve_d > sen_d ) {
        len_w = wal_u->eve_d - sen_d;
      }
    }
  }

  //  send batch
  //
  {
    u3_ovum* egg_u;
    u3_pico* pic_u;
    u3_noun    ovo, now, bit = u3qc_bex(48);

    {
      struct timeval tim_tv;
      gettimeofday(&tim_tv, 0);
      now = u3_time_in_tv(&tim_tv);
    }

    while ( len_w && car_u && (egg_u = u3_auto_next(car_u, &ovo)) ) {
      len_w--;
      u3_lord_work(god_u, egg_u, u3nc(u3k(now), ovo));
      now = u3ka_add(now, u3k(bit));

      //  queue events depth first
      //
      car_u = egg_u->car_u;

      //  interleave scry requests
      //
      if ( len_w && (pic_u = _pier_peek_next(pir_u)) )
      {
        len_w--;
        u3_lord_peek(god_u, pic_u);
        u3_pico_free(pic_u);
      }
    }

    //  if there's room left in the batch, fill it up with remaining scries
    //
    while ( len_w-- && (pic_u = _pier_peek_next(pir_u)) )
    {
      u3_lord_peek(god_u, pic_u);
      u3_pico_free(pic_u);
    }

    u3z(now); u3z(bit);
  }
}

/* _pier_gift_plan(): enqueue effects.
*/
static void
_pier_gift_plan(u3_work* wok_u, u3_gift* gif_u)
{
  u3_assert( gif_u->eve_d > wok_u->fec_u.rel_d );

#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): compute: complete\r\n", gif_u->eve_d);
#endif

  gif_u->nex_u = 0;

  if ( !wok_u->fec_u.ent_u ) {
    u3_assert( !wok_u->fec_u.ext_u );
    wok_u->fec_u.ent_u = wok_u->fec_u.ext_u = gif_u;
  }
  else {
    wok_u->fec_u.ent_u->nex_u = gif_u;
    wok_u->fec_u.ent_u = gif_u;
  }
}

/* _pier_gift_next(): dequeue effect.
*/
static u3_gift*
_pier_gift_next(u3_work* wok_u)
{
  u3_pier* pir_u = wok_u->pir_u;
  u3_disk* log_u = pir_u->log_u;
  u3_gift* gif_u = wok_u->fec_u.ext_u;

  if ( !gif_u || (gif_u->eve_d > log_u->dun_d) ) {
    return 0;
  }
  else {
    wok_u->fec_u.ext_u = gif_u->nex_u;

    if ( !wok_u->fec_u.ext_u ) {
      wok_u->fec_u.ent_u = 0;
    }

    u3_assert( (1ULL + wok_u->fec_u.rel_d) == gif_u->eve_d );
    wok_u->fec_u.rel_d = gif_u->eve_d;

    return gif_u;
  }
}

/* _pier_gift_kick(): apply effects.
*/
static void
_pier_gift_kick(u3_work* wok_u)
{
  u3_gift* gif_u;

  while ( (gif_u = _pier_gift_next(wok_u)) ) {
#ifdef VERBOSE_PIER
    fprintf(stderr, "pier: (%" PRIu64 "): compute: release\r\n", gif_u->eve_d);
#endif

    u3_auto_kick(wok_u->car_u, gif_u->act);
    u3_gift_free(gif_u);
  }
}

/* _pier_wall_plan(): enqueue a barrier.
*/
static void
_pier_wall_plan(u3_pier* pir_u, c3_d eve_d,
                void* ptr_v, void (*wal_f)(void*, c3_d))
{
  u3_assert( u3_psat_work == pir_u->sat_e );

  u3_wall* wal_u = c3_malloc(sizeof(*wal_u));
  wal_u->ptr_v = ptr_v;
  wal_u->eve_d = eve_d;
  wal_u->wal_f = wal_f;

  //  insert into [pir_u->wal_u], preserving stable sort by [eve_d]
  //
  {
    u3_wall** las_u = &pir_u->wok_u->wal_u;

    while ( *las_u && (eve_d <= (*las_u)->eve_d) ) {
      las_u = &(*las_u)->nex_u;
    }

    wal_u->nex_u = *las_u;
    *las_u = wal_u;
  }
}

/* _pier_wall(): process a barrier if possible.
*/
static void
_pier_wall(u3_work* wok_u)
{
  u3_lord* god_u = wok_u->pir_u->god_u;
  u3_disk* log_u = wok_u->pir_u->log_u;

  if ( god_u->eve_d == log_u->dun_d ) {
    u3_wall* wal_u;

    while (  (wal_u = wok_u->wal_u)
          && !god_u->dep_w
          && (wal_u->eve_d <= god_u->eve_d) )
    {
      wok_u->wal_u = wal_u->nex_u;
      wal_u->wal_f(wal_u->ptr_v, god_u->eve_d);
      c3_free(wal_u);
    }
  }
}

/* _pier_work(): advance event processing.
*/
static void
_pier_work(u3_work* wok_u)
{
  u3_pier* pir_u = wok_u->pir_u;

  if ( c3n == pir_u->liv_o ) {
    pir_u->liv_o = u3_auto_live(wok_u->car_u);

    //  all i/o drivers are fully initialized
    //
    if ( c3y == pir_u->liv_o ) {
      //  XX this is when "boot" is actually complete
      //  XX even better would be after neighboring with our sponsor
      //
      u3l_log("pier (%" PRIu64 "): live", pir_u->god_u->eve_d);

      //  XX move callbacking to king
      //
      if ( u3_Host.bot_f ) {
        u3_Host.bot_f();
      }
    }
  }

  _pier_gift_kick(wok_u);
  _pier_wall(wok_u);

  if ( u3_psat_work == pir_u->sat_e ) {
    _pier_work_send(wok_u);
  }
  else {
    u3_assert( u3_psat_done == pir_u->sat_e );
  }
}

/* _pier_on_lord_work_spin(): start spinner
*/
static void
_pier_on_lord_work_spin(void* ptr_v, u3_atom pin, c3_o del_o)
{
  u3_pier* pir_u = ptr_v;

  u3_assert(  (u3_psat_wyrd == pir_u->sat_e)
           || (u3_psat_work == pir_u->sat_e)
           || (u3_psat_done == pir_u->sat_e) );

  u3_term_start_spinner(pin, del_o);
}

/* _pier_on_lord_work_spun(): stop spinner
*/
static void
_pier_on_lord_work_spun(void* ptr_v)
{
  u3_pier* pir_u = ptr_v;

  u3_assert(  (u3_psat_wyrd == pir_u->sat_e)
           || (u3_psat_work == pir_u->sat_e)
           || (u3_psat_done == pir_u->sat_e) );

  u3_term_stop_spinner();
}

/* _pier_on_lord_work_done(): event completion from worker.
*/
static void
_pier_on_lord_work_done(void*    ptr_v,
                        u3_ovum* egg_u,
                        u3_fact* tac_u,
                        u3_gift* gif_u)
{
  u3_pier* pir_u = ptr_v;

  u3_assert(  (u3_psat_work == pir_u->sat_e)
           || (u3_psat_done == pir_u->sat_e) );

#ifdef VERBOSE_PIER
  fprintf(stderr, "pier (%" PRIu64 "): work: done\r\n", tac_u->eve_d);
#endif

  //  XX this is a departure from the general organization of this file
  //
  u3_disk_plan(pir_u->log_u, tac_u);

  u3_auto_done(egg_u);

  _pier_gift_plan(pir_u->wok_u, gif_u);
  _pier_work(pir_u->wok_u);
}

/* _pier_on_lord_work_bail(): event failure from worker.
*/
static void
_pier_on_lord_work_bail(void* ptr_v, u3_ovum* egg_u, u3_noun lud)
{
  u3_pier* pir_u = ptr_v;

#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: work: bail\r\n");
#endif

  u3_assert(  (u3_psat_work == pir_u->sat_e)
           || (u3_psat_done == pir_u->sat_e) );

  u3_auto_bail(egg_u, lud);

  //  XX groace
  //
  if ( pir_u->wok_u ) {
    _pier_work(pir_u->wok_u);
  }
}

/* _pier_work_time(): set time.
*/
static void
_pier_work_time(u3_pier* pir_u)
{
  struct timeval tim_tv;
  gettimeofday(&tim_tv, 0);

  // XX save to pier
  //
  u3v_time(u3_time_in_tv(&tim_tv));
}

/* _pier_work_fore_cb(): run on every loop iteration before i/o polling.
*/
static void
_pier_work_fore_cb(uv_prepare_t* pep_u)
{
  u3_work* wok_u = pep_u->data;
  _pier_work_time(wok_u->pir_u);
}

/* _pier_work_afte_cb(): run on every loop iteration after i/o polling.
*/
static void
_pier_work_afte_cb(uv_check_t* cek_u)
{
  u3_work* wok_u = cek_u->data;
  _pier_work(wok_u);
}

/* _pier_work_idle_cb(): run on next loop iteration.
*/
static void
_pier_work_idle_cb(uv_idle_t* idl_u)
{
  u3_work* wok_u = idl_u->data;
  _pier_work(wok_u);
  uv_idle_stop(idl_u);
}

/* u3_pier_spin(): (re-)activate idle handler
*/
void
u3_pier_spin(u3_pier* pir_u)
{
  //  XX return c3n instead?
  //
  if (  u3_psat_work == pir_u->sat_e
     || u3_psat_done == pir_u->sat_e )
  {
    u3_work* wok_u = pir_u->wok_u;

    if ( !uv_is_active((uv_handle_t*)&wok_u->idl_u) ) {
      uv_idle_start(&wok_u->idl_u, _pier_work_idle_cb);
    }
  }
}

/* u3_pier_peek(): read namespace.
*/
void
u3_pier_peek(u3_pier*   pir_u,
             u3_noun      gan,
             u3_noun      ful,
             void*      ptr_v,
             u3_peek_cb fun_f)
{
  u3_pico* pic_u = u3_pico_init();

  pic_u->ptr_v = ptr_v;
  pic_u->fun_f = fun_f;
  pic_u->gan   = gan;
  //
  pic_u->typ_e = u3_pico_full;
  pic_u->ful   = ful;

  _pier_peek_plan(pir_u, pic_u);
}

/* u3_pier_peek_last(): read namespace, injecting ship and case.
*/
void
u3_pier_peek_last(u3_pier*   pir_u,
                  u3_noun      gan,
                  c3_m       car_m,
                  u3_atom      des,
                  u3_noun      pax,
                  void*      ptr_v,
                  u3_peek_cb fun_f)
{
  u3_pico* pic_u = u3_pico_init();

  pic_u->ptr_v = ptr_v;
  pic_u->fun_f = fun_f;
  pic_u->gan   = gan;
  //
  pic_u->typ_e       = u3_pico_once;
  pic_u->las_u.car_m = car_m;
  pic_u->las_u.des   = des;
  pic_u->las_u.pax   = pax;

  _pier_peek_plan(pir_u, pic_u);
}

/* _pier_stab(): parse path
*/
static u3_noun
_pier_stab(u3_noun pac)
{
  return u3do("stab", pac);
}

/* _pier_on_scry_done(): scry callback.
*/
static void
_pier_on_scry_done(void* ptr_v, u3_noun nun)
{
  u3_pier* pir_u = ptr_v;
  u3_weak    res = u3r_at(7, nun);

  if (u3_none == res) {
    u3l_log("pier: scry failed");
  }
  else {
    u3_weak out;
    c3_c *ext_c, *pac_c;

    u3l_log("pier: scry succeeded");

    if ( u3_Host.ops_u.puk_c ) {
      pac_c = u3_Host.ops_u.puk_c;
    }
    else {
      pac_c = u3_Host.ops_u.pek_c + 1;
    }

    //  try to serialize as requested
    //
    {
      u3_atom puf = u3i_string(u3_Host.ops_u.puf_c);
      if ( c3y == u3r_sing(c3__jam, puf) ) {
        c3_d len_d;
        c3_y* byt_y;
        u3s_jam_xeno(res, &len_d, &byt_y);
        out = u3i_bytes(len_d, byt_y);
        ext_c = "jam";
        free(byt_y);
      }
      else if ( c3y == u3a_is_atom(res) ) {
        out   = u3dc("scot", u3k(puf), u3k(res));
        ext_c = "txt";
      }
      else {
        u3l_log("pier: cannot export cell as %s", u3_Host.ops_u.puf_c);
        out   = u3_none;
      }
      u3z(puf);
    }

    //  if serialization and export path succeeded, write to disk
    //
    if ( u3_none != out ) {
      c3_c fil_c[256];
      snprintf(fil_c, 256, "%s.%s", pac_c, ext_c);

      u3_unix_save(fil_c, out);
      u3l_log("pier: scry result in %s/.urb/put/%s", u3_Host.dir_c, fil_c);
    }
  }

  u3l_log("pier: exit");
  u3_pier_exit(pir_u);

  u3z(nun);
}

static c3_c*
_resolve_czar(u3_work* wok_u, c3_c* who_c)
{
  u3_noun czar = u3dc("scot", 'p', wok_u->pir_u->who_d[0] & ((1 << 8) - 1));
  c3_c* czar_c = u3r_string(czar);

  c3_c url[256];
  c3_w  len_w;
  c3_y* hun_y;

  sprintf(url, "https://%s.urbit.org/~/sponsor/%s", czar_c+1, who_c);

  c3_i ret_i = king_curl_bytes(url, &len_w, &hun_y, 1, 1);
  if (!ret_i) {
    c3_free(czar_c);
    czar_c = (c3_c*)hun_y;
  }

  u3z(czar);
  return czar_c;
}

static c3_o
_czar_boot_data(c3_c* czar_c,
                c3_c* who_c,
                c3_w* bone_w,
                c3_w* czar_glx_w,
                c3_w* czar_ryf_w,
                c3_w* czar_lyf_w,
                c3_w* czar_bon_w,
                c3_w* czar_ack_w)
{
  c3_c url[256];
  c3_w  len_w;
  c3_y* hun_y = 0;

  if ( bone_w != NULL ) {
    sprintf(url, "https://%s.urbit.org/~/boot/%s/%d",
            czar_c+1, who_c, *bone_w );
  } else {
    sprintf(url, "https://%s.urbit.org/~/boot/%s", czar_c+1, who_c);
  }

  c3_o ret_o = c3n;
  c3_i ret_i = king_curl_bytes(url, &len_w, &hun_y, 1, 1);
  if ( !ret_i ) {
    u3_noun jamd = u3i_bytes(len_w, hun_y);
    u3_noun cued = u3qe_cue(jamd);

    u3_noun czar_glx, czar_ryf, czar_lyf, czar_bon, czar_ack;

    if ( (c3y == u3r_hext(cued, 0, &czar_glx, &czar_ryf,
                          &czar_lyf, &czar_bon, &czar_ack)) &&
         (c3y == u3r_safe_word(czar_glx, czar_glx_w)) &&
         (c3y == u3r_safe_word(czar_ryf, czar_ryf_w)) &&
         (c3y == u3r_safe_word(czar_lyf, czar_lyf_w)) ) {
      if ( c3y == u3du(czar_bon) ) u3r_safe_word(u3t(czar_bon), czar_bon_w);
      if ( c3y == u3du(czar_ack) ) u3r_safe_word(u3t(czar_ack), czar_ack_w);
      ret_o = c3y;
    }

    u3z(jamd);
    u3z(cued);
    c3_free(hun_y);
  }

  return ret_o;
}

static void
_boot_scry_cb(void* vod_p, u3_noun nun)
{
  u3_work* wok_u = (u3_work*)vod_p;

  u3_atom who = u3dc("scot", c3__p, u3i_chubs(2, wok_u->pir_u->who_d));
  c3_c*   who_c = u3r_string(who);

  u3_noun rem, glx, ryf, bon, cur, nex;
  c3_w    glx_w, ryf_w, bon_w, cur_w, nex_w;

  c3_w czar_glx_w, czar_ryf_w, czar_lyf_w, czar_bon_w, czar_ack_w;
  czar_glx_w = czar_ryf_w = czar_lyf_w = czar_bon_w = czar_ack_w = u3_none;

  if ( (c3y == u3r_qual(nun, 0, 0, 0, &rem)) &&
       (c3y == u3r_hext(rem, &glx, &ryf, 0, &bon, &cur, &nex)) ) {
    /*
     * Boot scry succeeded. Proceed to cross reference networking state against
     * sponsoring galaxy.
     */
    glx_w = u3r_word(0, glx); ryf_w = u3r_word(0, ryf);
    bon_w = u3r_word(0, bon); cur_w = u3r_word(0, cur);
    nex_w = u3r_word(0, nex);

    u3_atom czar = u3dc("scot", c3__p, glx_w);
    c3_c*   czar_c = u3r_string(czar);

    if ( c3n == _czar_boot_data(czar_c, who_c, &bon_w,
                                &czar_glx_w, &czar_ryf_w,
                                &czar_lyf_w, &czar_bon_w,
                                &czar_ack_w) ) {
      u3l_log("boot: peer-state unvailable on czar, cannot protect from double-boot");
      _pier_work(wok_u);
    } else {
      if ( czar_ryf_w == ryf_w ) {
        c3_w ack_w = cur_w - 1;
        if ( czar_ack_w == u3_none ) {
          // This codepath should never be hit
          u3l_log("boot: message-sink-state unvailable on czar, cannot protect from double-boot");
          _pier_work(wok_u);
        } else if ( ( nex_w - cur_w ) >= ( czar_ack_w - ack_w ) ) {
          _pier_work(wok_u);
        } else {
          u3l_log("boot: failed: double-boot detected, refusing to boot %s\r\n"
                  "this ship is an old copy, resume the latest version of the ship or breach\r\n"
                  "read more: https://docs.urbit.org/glossary/double-boot",
                  who_c);
          u3_king_bail();
        }
      } else {
        // Trying to boot old ship after breach
        u3l_log("boot: failed: double-boot detected, refusing to boot %s\r\n"
                "you are trying to boot an existing ship from a keyfile,"
                "resume the latest version of the ship or breach\r\n"
                "read more: https://docs.urbit.org/glossary/double-boot",
                who_c);
        u3_king_bail();
      }
    }

    u3z(czar);
    c3_free(czar_c);
  } else if ( c3y == u3r_trel(nun, 0, 0, &rem) && rem == 0 ) {
    /*
     * Data not available for boot scry. Check against sponsoring galaxy.
     * If peer state exists exit(1) unless ship has breached,
     * otherwise continue boot.
     */
    c3_c* czar_c = _resolve_czar(wok_u, who_c);

    if ( c3n == _czar_boot_data(czar_c, who_c, 0,
                                &czar_glx_w, &czar_ryf_w,
                                &czar_lyf_w, &czar_bon_w, 0) ) {
      c3_free(czar_c);
      _pier_work(wok_u);
    } else {
      // Peer state found under czar
      c3_free(czar_c);
      u3_weak kf_ryf = wok_u->pir_u->ryf;
      if ( kf_ryf == u3_none ) {
        u3l_log("boot: keyfile rift unavailable, cannot protect from double-boot");
        _pier_work(wok_u);
      } else if ( kf_ryf > czar_ryf_w ) {
        // Ship breached, galaxy has not heard about the breach; continue boot
        _pier_work(wok_u);
      } else if ( (     kf_ryf == czar_ryf_w ) &&
                  ( czar_bon_w == u3_none ) ) {
        // Ship has breached, continue boot
        _pier_work(wok_u);
      } else {
        u3l_log("boot: failed: double-boot detected, refusing to boot %s\r\n"
                "this ship has already been booted elsewhere, "
                "boot the existing pier or breach\r\n"
                "read more: https://docs.urbit.org/glossary/double-boot",
                who_c);
        u3_king_bail();
      }
    }
  } else {
    /*
     * Boot scry endpoint doesn't exists. Most likely old arvo.
     * Continue boot and hope for the best.
     */
    u3l_log("boot: %%boot scry endpoint doesn't exist, cannot protect from double-boot");
    _pier_work(wok_u);
  }
  u3z(nun); u3z(who);
  c3_free(who_c);
}

/* _pier_work_init(): begin processing new events
*/
static void
_pier_work_init(u3_pier* pir_u)
{
  u3_work* wok_u;

  u3_assert( u3_psat_wyrd == pir_u->sat_e );

  pir_u->sat_e = u3_psat_work;
  pir_u->wok_u = wok_u = c3_calloc(sizeof(*wok_u));
  wok_u->pir_u = pir_u;
  wok_u->fec_u.rel_d = pir_u->log_u->dun_d;

  _pier_work_time(pir_u);

  //  XX plan kelvin event
  //

  //  XX snapshot timer
  //  XX moveme
  //
  {
    c3_l cod_l = u3a_lush(c3__save);
    u3_save_io_init(pir_u);
    u3a_lop(cod_l);
  }

  //  initialize pre i/o polling handle
  //
  uv_prepare_init(u3L, &wok_u->pep_u);
  wok_u->pep_u.data = wok_u;
  uv_prepare_start(&wok_u->pep_u, _pier_work_fore_cb);

  //  initialize post i/o polling handle
  //
  uv_check_init(u3L, &wok_u->cek_u);
  wok_u->cek_u.data = wok_u;
  uv_check_start(&wok_u->cek_u, _pier_work_afte_cb);

  //  initialize idle i/o polling handle
  //
  //    NB, not started
  //
  uv_idle_init(u3L, &wok_u->idl_u);
  wok_u->idl_u.data = wok_u;

  // //  setup u3_lord work callbacks
  // //
  // u3_lord_work_cb cb_u = {
  //   .ptr_v  = wok_u,
  //   .spin_f = _pier_on_lord_work_spin,
  //   .spun_f = _pier_on_lord_work_spun,
  //   .done_f = _pier_on_lord_work_done,
  //   .bail_f = _pier_on_lord_work_bail
  // };
  // u3_lord_work_init(pir_u->god_u, cb_u);

  //  XX this is messy, revise
  //
  if ( u3_Host.ops_u.pek_c ) {
    u3_noun pex = u3do("stab", u3i_string(u3_Host.ops_u.pek_c));
    u3_noun car;
    u3_noun dek;
    u3_noun pax;
    if ( c3n == u3r_trel(pex, &car, &dek, &pax)
      || c3n == u3a_is_cat(car) )
    {
      u3m_p("pier: invalid scry", pex);
      _pier_on_scry_done(pir_u, u3_nul);
    } else {
      //  run the requested scry, jam to disk, then exit
      //
      u3l_log("pier: scry");
      u3_pier_peek_last(pir_u, u3_nul, u3k(car), u3k(dek), u3k(pax),
                        pir_u, _pier_on_scry_done);
    }
    u3z(pex);
  }
  else {
    //  initialize i/o drivers
    //
    wok_u->car_u = u3_auto_init(pir_u);
    u3_auto_talk(wok_u->car_u);
  }

  c3_d pi_d = wok_u->pir_u->who_d[0];
  c3_d pt_d = wok_u->pir_u->who_d[1];

  if ( (pi_d < 256 && pt_d == 0) || (c3n == u3_Host.ops_u.net) ) {
    // Skip double boot protection for galaxies and local mode ships
    //
    _pier_work(wok_u);
  } else {
    // Double boot protection
    //
    u3_noun pex = u3nc(u3i_string("boot"), u3_nul);
    u3_pier_peek_last(pir_u, u3nc(u3_nul, u3_nul), c3__ax, u3_nul, pex,
                      pir_u->wok_u, _boot_scry_cb);
  }
}

/* _pier_wyrd_good(): %wyrd version negotation succeeded.
*/
static void
_pier_wyrd_good(u3_pier* pir_u, u3_ovum* egg_u)
{
  //  restore event callbacks
  //
  {
    u3_lord* god_u = pir_u->god_u;
    god_u->cb_u.work_done_f = _pier_on_lord_work_done;
    god_u->cb_u.work_bail_f = _pier_on_lord_work_bail;
  }

  //  initialize i/o drivers
  //
  _pier_work_init(pir_u);

  //  free %wyrd driver and ovum
  //
  {
    u3_auto* car_u = egg_u->car_u;
    u3_auto_done(egg_u);
    c3_free(car_u);
  }
}

/* _pier_wyrd_fail(): %wyrd version negotation failed.
*/
static void
_pier_wyrd_fail(u3_pier* pir_u, u3_ovum* egg_u, u3_noun lud)
{
  //  XX version negotiation failed, print upgrade message
  //
  u3l_log("pier: version negotation failed\n");

  //  XX only print trace with -v ?
  //
  if ( u3_nul != lud ) {
    u3_auto_bail_slog(egg_u, u3k(u3t(lud)));
  }

  u3z(lud);

  //  free %wyrd driver and ovum
  //
  {
    u3_auto* car_u = egg_u->car_u;
    u3_auto_done(egg_u);
    c3_free(car_u);
  }

  u3_pier_bail(pir_u);
}

//  XX organizing version constants
//
#define VERE_NAME  "vere"
#define VERE_ZUSE  410
#define VERE_LULL  322

/* _pier_wyrd_aver(): check for %wend effect and version downgrade. RETAIN
*/
static c3_o
_pier_wyrd_aver(u3_noun act)
{
  u3_noun fec, kel, ver;

  //    XX review, %wend re: %wyrd optional?
  //
  while ( u3_nul != act ) {
    u3x_cell(act, &fec, &act);

    if ( c3__wend == u3h(fec) ) {
      kel = u3t(fec);

      //  traverse $wynn, check for downgrades
      //
      while ( u3_nul != kel ) {
        u3x_cell(kel, &ver, &kel);

        //  check for %zuse downgrade
        //
        if (  (c3__zuse == u3h(ver))
           && (VERE_ZUSE != u3t(ver)) )
        {
          return c3n;
        }

        //  XX in the future, send %wend to serf
        //  to also negotiate downgrade of nock/hoon/&c?
        //  (we don't want to have to filter effects)
        //
      }
    }
  }

  return c3y;
}

/* _pier_on_lord_wyrd_done(): callback for successful %wyrd event.
*/
static void
_pier_on_lord_wyrd_done(void*    ptr_v,
                        u3_ovum* egg_u,
                        u3_fact* tac_u,
                        u3_gift* gif_u)
{
  u3_pier* pir_u = ptr_v;

  u3_assert( u3_psat_wyrd == pir_u->sat_e );

  //  arvo's side of version negotiation succeeded
  //  traverse [gif_y] and validate
  //
  if ( c3n == _pier_wyrd_aver(gif_u->act) ) {
    u3_fact_free(tac_u);
    u3_gift_free(gif_u);

    //  XX messaging, cli argument to bypass
    //
    u3l_log("pier: version negotiation failed; downgrade");
    _pier_wyrd_fail(pir_u, egg_u, u3_nul);
  }
  else {
    //  enqueue %wyrd event-log commit
    //
    u3_disk_plan(pir_u->log_u, tac_u);

    //  finalize %wyrd success
    //
    _pier_wyrd_good(pir_u, egg_u);

    //  plan %wyrd effects
    //
    _pier_gift_plan(pir_u->wok_u, gif_u);
  }
}

/* _pier_on_lord_wyrd_bail(): callback for failed %wyrd event.
*/
static void
_pier_on_lord_wyrd_bail(void* ptr_v, u3_ovum* egg_u, u3_noun lud)
{
  u3_pier* pir_u = ptr_v;

  u3_assert( u3_psat_wyrd == pir_u->sat_e );

  //  XX add cli argument to bypass negotiation failure
  //
#if 1
  //  print %wyrd failure and exit
  //
  //    XX check bail mote, retry on %intr, %meme, &c
  //
  _pier_wyrd_fail(pir_u, egg_u, lud);
#else
  //  XX temporary hack to fake %wyrd success
  //
  {
    _pier_wyrd_good(pir_u, egg_u);
    u3z(lud);
  }
#endif
}

/* _pier_wyrd_card(): construct %wyrd.
*/
static u3_noun
_pier_wyrd_card(u3_pier* pir_u)
{
  u3_lord* god_u = pir_u->god_u;
  u3_noun    sen;

  _pier_work_time(pir_u);

  {
    c3_l  sev_l;
    u3_noun now;
    struct timeval tim_u;
    gettimeofday(&tim_u, 0);

    now   = u3_time_in_tv(&tim_u);
    sev_l = u3r_mug(now);
    sen   = u3dc("scot", c3__uv, sev_l);

    u3z(now);
  }

  //  XX god_u not necessarily available yet, refactor call sites
  //
  u3_noun ver = u3nq(u3i_string(VERE_NAME),
                     u3i_string(U3_VERE_PACE),
                     u3dc("scot", c3__ta, u3i_string(URBIT_VERSION)),
                     u3_nul);
  u3_noun kel = u3nl(u3nc(c3__zuse, VERE_ZUSE),  //  XX from both king and serf?
                     u3nc(c3__lull, VERE_LULL),  //  XX from both king and serf?
                     u3nc(c3__arvo, 236),        //  XX from both king and serf?
                     u3nc(c3__hoon, 137),        //  god_u->hon_y
                     u3nc(c3__nock, 4),          //  god_u->noc_y
                     u3_none);
  u3_noun wir = u3nc(c3__arvo, u3_nul);
  return u3nt(c3__wyrd, u3nc(sen, ver), kel);
}

/* _pier_wyrd_init(): send %wyrd.
*/
static void
_pier_wyrd_init(u3_pier* pir_u)
{
  u3_noun cad = _pier_wyrd_card(pir_u);
  u3_noun wir = u3nc(c3__arvo, u3_nul);

  pir_u->sat_e = u3_psat_wyrd;

  u3l_log("vere: checking version compatibility");

  {
    u3_lord* god_u = pir_u->god_u;
    u3_auto* car_u = c3_calloc(sizeof(*car_u));
    u3_ovum* egg_u = u3_ovum_init(0, u3_blip, wir, cad);
    u3_noun    ovo;

    car_u->pir_u = pir_u;
    car_u->nam_m = c3__wyrd;

    u3_auto_plan(car_u, egg_u);

    //  instead of subscribing with u3_auto_peer(),
    //  we swizzle the [god_u] callbacks for full control
    //
    god_u->cb_u.work_done_f = _pier_on_lord_wyrd_done;
    god_u->cb_u.work_bail_f = _pier_on_lord_wyrd_bail;

    u3_assert( u3_auto_next(car_u, &ovo) == egg_u );

    {
      struct timeval tim_tv;
      gettimeofday(&tim_tv, 0);
      u3_lord_work(god_u, egg_u, u3nc(u3_time_in_tv(&tim_tv), ovo));
    }
  }
}

/* _pier_play_plan(): enqueue events for replay.
*/
static void
_pier_play_plan(u3_play* pay_u, u3_info fon_u)
{
  u3_fact** ext_u;
  c3_d      old_d;

  if ( !pay_u->ext_u ) {
    u3_assert( !pay_u->ent_u );
    ext_u = &pay_u->ext_u;
    old_d = pay_u->sen_d;
  }
  else {
    ext_u = &pay_u->ent_u->nex_u;
    old_d = pay_u->ent_u->eve_d;
  }

#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: play plan %" PRIu64 "-%" PRIu64 " at %" PRIu64 "\r\n",
                  fon_u.ext_u->eve_d,
                  fon_u.ent_u->eve_d,
                  old_d);
#endif

  u3_assert( (1ULL + old_d) == fon_u.ext_u->eve_d );

  *ext_u = fon_u.ext_u;
  pay_u->ent_u = fon_u.ent_u;
}

/* _pier_play_send(): detach a batch of up to [len_w] events from queue.
*/
static u3_info
_pier_play_next(u3_play* pay_u, c3_w len_w)
{
  u3_fact* tac_u = pay_u->ext_u;
  u3_info  fon_u;

  //  XX just share batch with lord, save last sent to pay_u->sen_u
  //

  //  set batch entry and exit pointers
  //
  {
    fon_u.ext_u = tac_u;

    while ( len_w-- && tac_u->nex_u ) {
      tac_u = tac_u->nex_u;
    }

    fon_u.ent_u = tac_u;
  }

  //  detatch batch from queue
  //
  if ( tac_u->nex_u ) {
    pay_u->ext_u = tac_u->nex_u;
    tac_u->nex_u = 0;
  }
  else {
    pay_u->ent_u = pay_u->ext_u = 0;
  }

  return fon_u;
}

/* _pier_play_send(): send a batch of events to the worker for replay.
*/
static void
_pier_play_send(u3_play* pay_u)
{
  u3_pier* pir_u = pay_u->pir_u;
  c3_w     len_w;

  //  awaiting read
  //
  if ( !pay_u->ext_u ) {
    return;
  }

  //  XX fill the pipe how much?
  // (god_u->dep_w > PIER_WORK_BATCH) )
  //

  //  the first batch must be >= the lifecycle barrier
  //
  if ( !pay_u->sen_d ) {
    len_w = c3_max(pir_u->lif_w, PIER_PLAY_BATCH);
  }
  else {
    c3_d lef_d = (pay_u->eve_d - pay_u->sen_d);
    len_w = c3_min(lef_d, PIER_PLAY_BATCH);
  }

  {
    u3_info fon_u = _pier_play_next(pay_u, len_w);

    //  bump sent counter
    //
    pay_u->sen_d = fon_u.ent_u->eve_d;

#ifdef VERBOSE_PIER
    fprintf(stderr, "pier: play send %" PRIu64 "-%" PRIu64 "\r\n", fon_u.ext_u->eve_d, fon_u.ent_u->eve_d);
#endif

    u3_lord_play(pir_u->god_u, fon_u);
  }
}

/* _pier_play_read(): read events from disk for replay.
*/
static void
_pier_play_read(u3_play* pay_u)
{
  u3_pier* pir_u = pay_u->pir_u;
  c3_d las_d;

  if ( pay_u->ent_u ) {
    las_d = pay_u->ent_u->eve_d;

    //  cap the pir_u->pay_u queue depth
    //
    if ( (las_d - pay_u->ext_u->eve_d) >= PIER_PLAY_BATCH ) {
      return;
    }
  }
  else {
    las_d = pay_u->sen_d;
  }

  {
    c3_d nex_d = (1ULL + las_d);
    c3_d len_d = c3_min(pay_u->eve_d - las_d, PIER_READ_BATCH);

    if (  len_d
       && (nex_d > pay_u->req_d) )
    {
      u3_disk_read(pir_u->log_u, nex_d, len_d);
      pay_u->req_d = nex_d;

#ifdef VERBOSE_PIER
      fprintf(stderr, "pier: play read %" PRIu64 " at %" PRIu64 "\r\n", len_d, nex_d);
#endif
    }
  }
}

/* _pier_play(): send a batch of events to the worker for log replay.
*/
static void
_pier_play(u3_play* pay_u)
{
  u3_pier* pir_u = pay_u->pir_u;
  u3_lord* god_u = pir_u->god_u;
  u3_disk* log_u = pir_u->log_u;

  if ( god_u->eve_d == pay_u->eve_d ) {
    //  XX should be play_cb
    //
    u3l_log("---------------- playback complete ----------------");
    u3_term_stop_spinner();

    if ( pay_u->eve_d < log_u->dun_d ) {
      // u3l_log("pier: replay barrier reached, shutting down");
      // //  XX graceful shutdown
      // //
      // u3_lord_save(pir_u->god_u);
      // u3_pier_bail(pir_u);
      // exit(0);

      //  XX temporary hack
      //
      u3l_log("pier: replay barrier reached, cramming");
      u3_pier_cram(pir_u);
    }
    else if ( pay_u->eve_d == log_u->dun_d ) {
      u3_lord_save(pir_u->god_u);

      //  early exit, preparing for upgrade
      //
      //    XX check kelvins?
      //
      if ( c3y == u3_Host.pep_o ) {
        u3_pier_exit(pir_u);
      }
      else {
        _pier_wyrd_init(pir_u);
      }
    }
  }
  else {
    u3_assert( god_u->eve_d < pay_u->eve_d );
    _pier_play_send(pay_u);
    _pier_play_read(pay_u);
  }
}

/* _pier_on_lord_play_done(): log replay batch completion from worker.
*/
static void
_pier_on_lord_play_done(void* ptr_v, u3_info fon_u, c3_l mug_l)
{
  u3_pier* pir_u = ptr_v;
  u3_fact* tac_u = fon_u.ent_u;
  u3_fact* nex_u;

  u3_assert( u3_psat_play == pir_u->sat_e );

  u3l_log("pier: (%" PRIu64 "): play: done", tac_u->eve_d);

  //  XX optional
  //
  if ( tac_u->mug_l && (tac_u->mug_l != mug_l) ) {
    u3l_log("pier: (%" PRIu64 "): play: mug mismatch %x %x",
            tac_u->eve_d,
            tac_u->mug_l,
            mug_l);
    // u3_pier_bail(pir_u);
  }

  //  dispose successful
  //
  {
    tac_u = fon_u.ext_u;

    while ( tac_u ) {
      nex_u = tac_u->nex_u;
      u3_fact_free(tac_u);
      tac_u = nex_u;
    }
  }

  _pier_play(pir_u->pay_u);
}

/* _pier_on_lord_play_bail(): log replay batch failure from worker.
*/
static void
_pier_on_lord_play_bail(void* ptr_v, u3_info fon_u,
                        c3_l mug_l, c3_d eve_d, u3_noun dud)
{
  u3_pier* pir_u = ptr_v;

  u3_assert( u3_psat_play == pir_u->sat_e );

  {
    u3_fact* tac_u = fon_u.ext_u;
    u3_fact* nex_u;
    c3_l     las_l = 0;

    //  dispose successful
    //
    while ( tac_u->eve_d <= eve_d ) {
      nex_u = tac_u->nex_u;
      las_l = tac_u->mug_l;
      u3_fact_free(tac_u);
      tac_u = nex_u;
    }

    //  XX optional
    //
    if ( las_l && (las_l != mug_l) ) {
      u3l_log("pier: (%" PRIu64 "): play bail: mug mismatch %x %x",
             (c3_d)(eve_d - 1ULL),
             las_l,
             mug_l);
      // u3_pier_bail(pir_u);
    }

    //  XX enable to retry
    //
#if 0
    {
      u3l_log("pier: (%" PRIu64 "): play: retry", eve_d);

      fon_u.ext_u = tac_u;

      //  we're enqueuing here directly onto the exit.
      //  like, _pier_play_plan() in reverse
      //
      if ( !pay_u->ext_u ) {
        pay_u->ext_u = fon_u.ext_u;
        pay_u->ent_u = fon_u.ent_u;
      }
      else {
        fon_u.ent_u->nex_u = pay_u->ext_u;
        pay_u->ext_u = fon_u.ext_u;
      }

      _pier_play(pir_u->pay_u);
      u3z(dud);
    }
#else
    {
      u3l_log("pier: (%" PRIu64 "): play: bail", eve_d);
      u3_pier_punt_goof("play", dud);
      {
        u3_noun wir, tag;
        u3x_qual(tac_u->job, 0, &wir, &tag, 0);
        u3_pier_punt_ovum("play", u3k(wir), u3k(tag));
      }

      u3_pier_bail(pir_u);
      exit(1);
    }
#endif
  }
}

/* _pier_play_init(): begin boot/replay up to [eve_d].
*/
static void
_pier_play_init(u3_pier* pir_u, c3_d eve_d)
{
  u3_lord* god_u = pir_u->god_u;
  u3_disk* log_u = pir_u->log_u;
  u3_play* pay_u;

  u3_assert(  (u3_psat_init == pir_u->sat_e)
           || (u3_psat_boot == pir_u->sat_e) );

  u3_assert( eve_d >  god_u->eve_d );
  u3_assert( eve_d <= log_u->dun_d );

  pir_u->sat_e = u3_psat_play;
  pir_u->pay_u = pay_u = c3_calloc(sizeof(*pay_u));
  pay_u->pir_u = pir_u;
  pay_u->eve_d = eve_d;
  pay_u->sen_d = god_u->eve_d;

  u3l_log("---------------- playback starting ----------------");
  if ( (1ULL + god_u->eve_d) == eve_d ) {
    u3l_log("pier: replaying event %" PRIu64, eve_d);
  }
  else {
    u3l_log("pier: replaying events %" PRIu64 "-%" PRIu64,
            (c3_d)(1ULL + god_u->eve_d),
            eve_d);
  }

  u3_term_start_spinner(c3__play, c3n);

  _pier_play(pay_u);
}

/* _pier_on_disk_read_done(): event log read success.
*/
static void
_pier_on_disk_read_done(void* ptr_v, u3_info fon_u)
{
  u3_pier* pir_u = ptr_v;

  u3_assert( u3_psat_play == pir_u->sat_e );

  _pier_play_plan(pir_u->pay_u, fon_u);
  _pier_play(pir_u->pay_u);
}

/* _pier_on_disk_read_bail(): event log read failure.
*/
static void
_pier_on_disk_read_bail(void* ptr_v, c3_d eve_d)
{
  u3_pier* pir_u = ptr_v;

  u3_assert( u3_psat_play == pir_u->sat_e );

  //  XX s/b play_bail_cb
  //
  fprintf(stderr, "pier: disk read bail\r\n");
  u3_term_stop_spinner();
  u3_pier_bail(pir_u);
}

/* _pier_on_disk_write_done(): event log write success.
*/
static void
_pier_on_disk_write_done(void* ptr_v, c3_d eve_d)
{
  u3_pier* pir_u = ptr_v;
  u3_disk* log_u = pir_u->log_u;

#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): db commit: complete\r\n", eve_d);
#endif

  if ( u3_psat_boot == pir_u->sat_e ) {
    //  lord already live
    //
    if ( c3y == pir_u->god_u->liv_o ) {
      //  XX print bootstrap commit complete
      //  XX s/b boot_complete_cb
      //
      _pier_play_init(pir_u, log_u->dun_d);
    }
  }
  else {
    u3_assert(  (u3_psat_work == pir_u->sat_e)
             || (u3_psat_done == pir_u->sat_e) );

    _pier_work(pir_u->wok_u);
  }
}

/* _pier_on_disk_write_bail(): event log write failure.
*/
static void
_pier_on_disk_write_bail(void* ptr_v, c3_d eve_d)
{
  u3_pier* pir_u = ptr_v;

  if ( u3_psat_boot == pir_u->sat_e ) {
    //  XX nice message
    //
  }

  // XX
  //
  fprintf(stderr, "pier: disk write bail\r\n");
  u3_pier_bail(pir_u);
}

/* _pier_on_lord_slog(): debug printf from worker.
*/
static void
_pier_on_lord_slog(void* ptr_v, c3_w pri_w, u3_noun tan)
{
  u3_pier* pir_u = ptr_v;

  if ( 0 != pir_u->sog_f ) {
    pir_u->sog_f(pir_u->sop_p, pri_w, u3k(tan));
  }

  u3_pier_tank(0, pri_w, tan);
}

/* _pier_on_lord_save(): worker (non-portable) snapshot complete.
*/
static void
_pier_on_lord_save(void* ptr_v)
{
  u3_pier* pir_u = ptr_v;

#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): lord: save\r\n", pir_u->god_u->eve_d);
#endif

  // _pier_next(pir_u);
}

/* _pier_on_lord_cram(): worker state-export complete (portable snapshot).
*/
static void
_pier_on_lord_cram(void* ptr_v)
{
  u3_pier* pir_u = ptr_v;

#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): lord: cram\r\n", pir_u->god_u->eve_d);
#endif

  //  XX temporary hack
  //
  if ( u3_psat_play == pir_u->sat_e ) {
    u3l_log("pier: cram complete, shutting down");
    u3_pier_bail(pir_u);
    exit(0);
  }

  // if ( u3_psat_done == pir_u->sat_e ) {
  //   fprintf(stderr, "snap cb exit\r\n");
  //   u3_lord_exit(pir_u->god_u);
  // }
  // else {
    // _pier_next(pir_u);
  // }
}

static void
_pier_done(u3_pier* pir_u);

/* _pier_on_lord_exit(): worker shutdown.
*/
static void
_pier_on_lord_exit(void* ptr_v)
{
  u3_pier* pir_u = ptr_v;

  //  the lord has already gone
  //
  pir_u->god_u = 0;

  if ( u3_psat_done != pir_u->sat_e ) {
    u3l_log("pier: serf shutdown unexpected");
    u3_pier_bail(pir_u);
  }
  //  if we made it all the way here, it's our jab to wrap up
  //
  else {
    _pier_done(pir_u);
  }
}

/* _pier_on_lord_bail(): worker error.
*/
static void
_pier_on_lord_bail(void* ptr_v)
{
  u3_pier* pir_u = ptr_v;

  //  the lord has already gone
  //
  pir_u->god_u = 0;

  u3_pier_bail(pir_u);
}

/* _pier_on_lord_live(): worker is ready.
*/
static void
_pier_on_lord_live(void* ptr_v)
{
  u3_pier* pir_u = ptr_v;
  u3_lord* god_u = pir_u->god_u;
  u3_disk* log_u = pir_u->log_u;

#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): boot at mug %x\r\n", god_u->eve_d, god_u->mug_l);
#endif

  u3_assert( god_u->eve_d <= log_u->dun_d );

  if ( u3_psat_boot == pir_u->sat_e ) {
    //  boot-sequence commit complete
    //
    if (  log_u->sen_d
       && (log_u->sen_d == log_u->dun_d) ) {
      //  XX print bootstrap commit complete
      //  XX s/b boot_complete_cb
      //
      //  XX this codepath should never be hit due to sync replay
      u3l_log("pier: warning: async replay");
      _pier_play_init(pir_u, log_u->dun_d);
    }
  }
  else {
    u3_assert( u3_psat_init == pir_u->sat_e );
    u3_assert( log_u->sen_d == log_u->dun_d );

    if ( god_u->eve_d < log_u->dun_d ) {
      c3_d eve_d;

      //  XX revisit
      //
      if (  u3_Host.ops_u.til_c ) {
        if ( 1 == sscanf(u3_Host.ops_u.til_c, "%" PRIu64 "", &eve_d) ) {
          u3l_log("pier: replay till %" PRIu64, eve_d);
        }
        else {
          u3l_log("pier: ignoring invalid replay barrier '%s'",
                  u3_Host.ops_u.til_c);
          eve_d = log_u->dun_d;
        }
      }
      else {
        eve_d = log_u->dun_d;
      }

      _pier_play_init(pir_u, eve_d);
    }
    else {
      //  early exit, preparing for upgrade
      //
      //    XX check kelvins?
      //
      if ( c3y == u3_Host.pep_o ) {
        u3_pier_exit(pir_u);
      }
      else {
        _pier_wyrd_init(pir_u);
      }
    }
  }
}

/* u3_pier_mass(): construct a $mass branch with noun/list.
*/
u3_noun
u3_pier_mass(u3_atom cod, u3_noun lit)
{
  return u3nt(cod, c3n, lit);
}

/* u3_pier_mase(): construct a $mass leaf.
*/
u3_noun
u3_pier_mase(c3_c* cod_c, u3_noun dat)
{
  return u3nt(u3i_string(cod_c), c3y, dat);
}

/* u3_pier_info(): pier status info as noun.
*/
u3_noun
u3_pier_info(u3_pier* pir_u)
{
  u3_noun nat;

  switch (pir_u->sat_e) {
    default: {
      nat = u3_pier_mass(u3i_string("state-unknown"), u3_nul);
    } break;

    case u3_psat_init: {
      nat = u3_pier_mass(c3__init, u3_nul);
    } break;

    case u3_psat_boot: {
      nat = u3_pier_mass(c3__boot, u3_nul);
    } break;

    case u3_psat_play: {
      u3_play* pay_u = pir_u->pay_u;

      nat = u3_pier_mass(c3__play,
        u3i_list(
          u3_pier_mase("target", u3i_chub(pay_u->eve_d)),
          u3_pier_mase("sent", u3i_chub(pay_u->sen_d)),
          u3_pier_mase("read", u3i_chub(pay_u->req_d)),
          u3_none));
    } break;

    case u3_psat_work: {
      u3_work*  wok_u = pir_u->wok_u;

      nat = u3_pier_mass(c3__work,
        u3i_list(
          u3_pier_mase("effects-released", u3i_chub(wok_u->fec_u.rel_d)),
          u3_pier_mase("pending-any", __(wok_u->fec_u.ext_u)),
          u3_pier_mase("pending-start",
                       ( wok_u->fec_u.ext_u
                         ? u3i_chub(wok_u->fec_u.ext_u->eve_d)
                         : 0 )),
          u3_pier_mase("pending-final",
                       ( wok_u->fec_u.ent_u
                         ? u3i_chub(wok_u->fec_u.ent_u->eve_d)
                         : 0 )),
          u3_pier_mase("wall-any", __(wok_u->wal_u)),
          u3_pier_mase("wall-event",
                       ( wok_u->wal_u
                         ? u3i_chub(wok_u->wal_u->eve_d)
                         : 0)),
          u3_pier_mass(c3__auto, u3_auto_info(wok_u->car_u)),
          u3_none));
    } break;

    case u3_psat_done: {
      nat = u3_pier_mass(c3__done, u3_nul);
    } break;
  }

  return u3_pier_mass(
    c3__pier,
    u3i_list(
      nat,
      u3_disk_info(pir_u->log_u),
      u3_lord_info(pir_u->god_u),
      u3_none));
}

/* u3_pier_slog(): print status info.
*/
void
u3_pier_slog(u3_pier* pir_u)
{
  switch ( pir_u->sat_e ) {
    default: {
      u3l_log("pier: unknown state: %u", pir_u->sat_e);
    } break;

    case u3_psat_init: {
      u3l_log("pier: init");
    } break;

    case u3_psat_boot: {
      u3l_log("pier: boot");
    } break;

    case u3_psat_play: {
      u3l_log("pier: play");

      {
        u3_play* pay_u = pir_u->pay_u;

        u3l_log("  target: %" PRIu64, pay_u->eve_d);
        u3l_log("  sent: %" PRIu64, pay_u->sen_d);
        u3l_log("  read: %" PRIu64, pay_u->req_d);
      }
    } break;

    case u3_psat_work: {
      u3l_log("pier: work");

      {
        u3_work* wok_u = pir_u->wok_u;

        u3l_log("  effects: released=%" PRIu64, wok_u->fec_u.rel_d);

        if ( wok_u->fec_u.ext_u ) {
          if ( wok_u->fec_u.ext_u != wok_u->fec_u.ent_u ) {
            u3l_log("    pending %" PRIu64 "-%" PRIu64,
                    wok_u->fec_u.ext_u->eve_d,
                    wok_u->fec_u.ent_u->eve_d);

          }
          else {
            u3l_log("    pending %" PRIu64, wok_u->fec_u.ext_u->eve_d);
          }
        }

        if ( wok_u->wal_u ) {
          u3l_log("  wall: %" PRIu64, wok_u->wal_u->eve_d);
        }

        if ( wok_u->car_u ) {
          u3_auto_slog(wok_u->car_u);
        }
      }
    } break;

    case u3_psat_done: {
      u3l_log("pier: done");
    } break;
  }

  if ( pir_u->log_u ) {
    u3_disk_slog(pir_u->log_u);
  }

  if ( pir_u->god_u ) {
    u3_lord_slog(pir_u->god_u);
  }
}

/* _pier_init(): create a pier, loading existing.
*/
static u3_pier*
_pier_init(c3_w wag_w, c3_c* pax_c, u3_weak ryf)
{
  //  create pier
  //
  u3_pier* pir_u = c3_calloc(sizeof(*pir_u));

  pir_u->pax_c = pax_c;
  pir_u->sat_e = u3_psat_init;
  pir_u->liv_o = c3n;
  pir_u->ryf   = ryf;

  // XX remove
  //
  pir_u->per_s = u3_Host.ops_u.per_s;
  pir_u->pes_s = u3_Host.ops_u.pes_s;
  pir_u->por_s = u3_Host.ops_u.por_s;
  pir_u->sav_u = c3_calloc(sizeof(u3_save));

  //  initialize persistence
  //
  {
    //  XX load/set secrets
    //
    u3_disk_cb cb_u = {
      .ptr_v = pir_u,
      .read_done_f = _pier_on_disk_read_done,
      .read_bail_f = _pier_on_disk_read_bail,
      .write_done_f = _pier_on_disk_write_done,
      .write_bail_f = _pier_on_disk_write_bail
    };

    if ( !(pir_u->log_u = u3_disk_init(pax_c, cb_u)) ) {
      c3_free(pir_u);
      return 0;
    }

    u3_assert( U3D_VERLAT == pir_u->log_u->ver_w );
  }

  //  initialize compute
  //
  {
    //  XX load/set secrets
    //
    c3_d tic_d[1];            //  ticket (unstretched)
    c3_d sec_d[1];            //  generator (unstretched)
    c3_d key_d[4];            //  secret (stretched)

    key_d[0] = key_d[1] = key_d[2] = key_d[3] = 0;

    u3_lord_cb cb_u = {
      .ptr_v = pir_u,
      .live_f = _pier_on_lord_live,
      .spin_f = _pier_on_lord_work_spin,
      .spun_f = _pier_on_lord_work_spun,
      .slog_f = _pier_on_lord_slog,
      .play_done_f = _pier_on_lord_play_done,
      .play_bail_f = _pier_on_lord_play_bail,
      .work_done_f = _pier_on_lord_work_done,
      .work_bail_f = _pier_on_lord_work_bail,
      .save_f = _pier_on_lord_save,
      .cram_f = _pier_on_lord_cram,
      .bail_f = _pier_on_lord_bail,
      .exit_f = _pier_on_lord_exit
    };

    if ( !(pir_u->god_u = u3_lord_init(pax_c, wag_w, key_d, cb_u)) )
    {
      // u3_disk_exit(pir_u->log_u)
      c3_free(pir_u);
      return 0;
    }
  }

  return pir_u;
}

/* u3_pier_stay(): restart an existing pier.
*/
u3_pier*
u3_pier_stay(c3_w wag_w, u3_noun pax)
{
  u3_pier* pir_u;
  u3_weak  rift = u3_none;

  if ( !(pir_u = _pier_init(wag_w, u3r_string(pax), rift)) ) {
    fprintf(stderr, "pier: stay: init fail\r\n");
    u3_king_bail();
    return 0;
  }

  if ( c3n == u3_disk_read_meta(pir_u->log_u->mdb_u,
                               &pir_u->log_u->ver_w,  pir_u->who_d,
                               &pir_u->fak_o,        &pir_u->lif_w) )
  {
    fprintf(stderr, "pier: disk read meta fail\r\n");
    //  XX dispose
    //
    u3_pier_bail(pir_u);
    exit(1);
  }

  if ( c3y == u3_Host.ops_u.veb ) {
    FILE* fil_u = u3_term_io_hija();
    u3_lmdb_stat(pir_u->log_u->mdb_u, fil_u);
    u3_term_io_loja(1, fil_u);
  }

  u3z(pax);

  return pir_u;
}

/* _pier_pill_parse(): extract boot formulas and module/userspace ova from pill
*/
static u3_boot
_pier_pill_parse(u3_noun pil)
{
  u3_boot bot_u;
  u3_noun pil_p, pil_q;

  u3_assert( c3y == u3du(pil) );
  u3x_cell(pil, &pil_p, &pil_q);

  {
    //  XX use faster cue
    //
    u3_noun pro = u3m_soft(0, u3ke_cue, u3k(pil_p));
    u3_noun mot, tag, dat;

    if (  (c3n == u3r_trel(pro, &mot, &tag, &dat))
       || (u3_blip != mot) )
    {
      u3m_p("mot", u3h(pro));
      fprintf(stderr, "boot: failed: unable to parse pill\r\n");
      u3_king_bail();
      exit(1);
    }

    if ( c3y == u3r_sing_c("ivory", tag) ) {
      fprintf(stderr, "boot: failed: unable to boot from ivory pill\r\n");
      u3_king_bail();
      exit(1);
    }
    else if ( c3__pill != tag ) {
      if ( c3y == u3a_is_atom(tag) ) {
        u3m_p("pill", tag);
      }
      fprintf(stderr, "boot: failed: unrecognized pill\r\n");
      u3_king_bail();
      exit(1);
    }

    {
      u3_noun typ;
      c3_c* typ_c;

      if ( c3n == u3r_qual(dat, &typ, &bot_u.bot, &bot_u.mod, &bot_u.use) ) {
        fprintf(stderr, "boot: failed: unable to extract pill\r\n");
        u3_king_bail();
        exit(1);
      }

      if ( c3y == u3a_is_atom(typ) ) {
        c3_c* typ_c = u3r_string(typ);
        fprintf(stderr, "boot: parsing %%%s pill\r\n", typ_c);
        c3_free(typ_c);
      }
    }

    u3k(bot_u.bot); u3k(bot_u.mod); u3k(bot_u.use);
    u3z(pro);
  }

  //  optionally replace filesystem in userspace
  //
  if ( u3_nul != pil_q ) {
    c3_w len_w = 0;
    u3_noun ova = bot_u.use;
    u3_noun new = u3_nul;
    u3_noun ovo;

    while ( u3_nul != ova ) {
      ovo = u3h(ova);

      u3_noun tag = u3h(u3t(ovo));
      if ( ( c3__into == tag ) || ( c3__park == tag ) ) {
        u3_assert( 0 == len_w );
        len_w++;
        ovo = u3t(pil_q);
      }

      new = u3nc(u3k(ovo), new);
      ova = u3t(ova);
    }

    u3_assert( 1 == len_w );

    u3z(bot_u.use);
    bot_u.use = u3kb_flop(new);
  }

  u3z(pil);

  return bot_u;
}

/* _pier_boot_make(): construct boot sequence
*/
static u3_boot
_pier_boot_make(u3_noun who,
                u3_noun wyr,
                u3_noun ven,
                u3_noun pil,
                u3_weak fed,
                u3_noun mor)
{
  u3_boot bot_u = _pier_pill_parse(pil); // transfer

  //  prepend entropy and identity to the module sequence
  //
  {
    u3_noun cad, wir = u3nt(u3_blip, c3__arvo, u3_nul);
    c3_w    eny_w[16];
    c3_rand(eny_w);

    cad = u3nt(c3__verb, u3_nul, ( c3y == u3_Host.ops_u.veb ) ? c3n : c3y);
    bot_u.mod = u3nc(u3nc(u3k(wir), cad), bot_u.mod);

    cad = u3nc(c3__wack, u3i_words(16, eny_w));
    bot_u.mod = u3nc(u3nc(u3k(wir), cad), bot_u.mod);

    cad = u3nc(c3__whom, who);                    // transfer [who]
    bot_u.mod = u3nc(u3nc(u3k(wir), cad), bot_u.mod);

    wir = u3nt(u3_blip, c3__arvo, u3_nul);
    bot_u.mod = u3nc(u3nc(wir, wyr), bot_u.mod);  // transfer [wir] and [wyr]
  }

  //  include additional key configuration events if we have multiple keys
  //
  if ( (u3_none != fed) && (c3y == u3du(u3h(fed))) && (u3h(u3h(fed))) == 1) {
    u3_noun wir = u3nt(c3__j, c3__seed, u3_nul);
    u3_noun tag = u3i_string("rekey");
    u3_noun kyz = u3t(u3t(fed));
    while ( u3_nul != kyz ) {
      u3_noun cad = u3nc(u3k(tag), u3k(u3h(kyz)));
      bot_u.use = u3nc(u3nc(u3k(wir), cad), bot_u.use);
      kyz = u3t(kyz);
    }
    u3z(tag); u3z(wir);
  }

  //  prepend legacy boot event to the userspace sequence
  //
  {
    //  XX do something about this wire
    //  XX route directly to %jael?
    //
    u3_assert( c3y == u3a_is_cell(ven) );

    u3_noun wir = u3nq(c3__d, c3__term, '1', u3_nul);
    u3_noun cad = u3nt(c3__boot, u3_Host.ops_u.lit, ven); // transfer

    bot_u.use = u3nc(u3nc(wir, cad), bot_u.use);
  }

  //  prepend & append additional boot enhancements to the userspace sequence
  //
  //    +$  prop  [%prop meta tier (list ovum)]
  //    +$  meta  term
  //    +$  tier  ?(%fore %hind)  ::  before or after userspace
  //
  {
    u3_noun mos = mor;
    u3_noun pre = u3_nul;
    u3_noun aft = u3_nul;
    while ( u3_nul != mos ) {
      u3_noun mot = u3h(mos);

      switch ( u3h(mot) ) {
        case c3__prop: {
          u3_noun ter, met, ves;

          if ( c3n == u3r_trel(u3t(mot), &met, &ter, &ves) ) {
            u3m_p("invalid prop", u3t(mot));
            break;
          }

          if ( c3__fore == ter ) {
            u3m_p("prop: fore", met);
            pre = u3kb_weld(pre, u3k(ves));
          }
          else if ( c3__hind == ter ) {
            u3m_p("prop: hind", met);
            aft = u3kb_weld(aft, u3k(ves));
          }
          else {
            u3m_p("unrecognized prop tier", ter);
          }
        } break;

        default: u3m_p("unrecognized boot sequence enhancement", u3h(mot));
      }

      mos = u3t(mos);
    }

    bot_u.use = u3kb_weld(pre, u3kb_weld(bot_u.use, aft)); // transfer
  }

  u3z(fed);
  u3z(mor);
  return bot_u;
}

/* _pier_boot_plan(): construct and commit boot sequence
*/
static c3_o
_pier_boot_plan(u3_pier* pir_u,
                u3_noun who,
                u3_noun ven,
                u3_noun pil,
                u3_weak fed,
                u3_noun mor)
{
  u3_boot bot_u;
  {
    pir_u->sat_e = u3_psat_boot;
    pir_u->fak_o = ( c3__fake == u3h(ven) ) ? c3y : c3n;
    u3r_chubs(0, 2, pir_u->who_d, who);

    bot_u = _pier_boot_make(who, _pier_wyrd_card(pir_u), ven, pil, fed, mor);
    pir_u->lif_w = u3qb_lent(bot_u.bot);
  }

  if ( c3n == u3_disk_save_meta(pir_u->log_u->mdb_u,
                                pir_u->log_u->ver_w, pir_u->who_d,
                                pir_u->fak_o,        pir_u->lif_w) )
  {
    //  XX dispose bot_u
    //
    return c3n;
  }

  if ( c3n == u3_disk_save_meta_meta(pir_u->log_u->com_u->pax_c,
                                     pir_u->who_d, pir_u->fak_o, pir_u->lif_w) )
  {
    fprintf(stderr, "disk: failed to save top-level metadata\r\n");
    return c3n;
  }

  //  insert boot sequence directly
  //
  //    Note that these are not ovum or (pair @da ovum) events,
  //    but raw nock formulas to be directly evaluated as the
  //    subject of the lifecycle formula [%2 [%0 3] %0 2].
  //    All subsequent events will be (pair date ovum).
  //
  {
    u3_noun fol = bot_u.bot;

    while ( u3_nul != fol ) {
      u3_disk_boot_plan(pir_u->log_u, u3k(u3h(fol)));
      fol = u3t(fol);
    }
  }

  //  insert module and userspace events
  //
  {
    u3_noun ova = bot_u.mod;
    u3_noun bit = u3qc_bex(48);   //  1/2^16 seconds
    u3_noun now;

    {
      struct timeval tim_tv;
      gettimeofday(&tim_tv, 0);
      now = u3_time_in_tv(&tim_tv);
    }


    while ( u3_nul != ova ) {
      u3_disk_boot_plan(pir_u->log_u, u3nc(u3k(now), u3k(u3h(ova))));
      now = u3ka_add(now, u3k(bit));
      ova = u3t(ova);
    }

    ova = bot_u.use;

    while ( u3_nul != ova ) {
      u3_disk_boot_plan(pir_u->log_u, u3nc(u3k(now), u3k(u3h(ova))));
      now = u3ka_add(now, u3k(bit));
      ova = u3t(ova);
    }

    u3z(bit); u3z(now);
  }

  u3_disk_boot_save(pir_u->log_u);

  u3z(bot_u.bot);
  u3z(bot_u.mod);
  u3z(bot_u.use);

  return c3y;
}

/* u3_pier_boot(): start a new pier.
*/
u3_pier*
u3_pier_boot(c3_w  wag_w,                   //  config flags
             u3_noun who,                   //  identity
             u3_noun ven,                   //  boot event
             u3_noun pil,                   //  type-of/path-to pill
             u3_noun pax,                   //  path to pier
             u3_weak fed,                   //  extra private keys
             u3_noun mor)                   //  extra boot sequence props
{
  u3_pier* pir_u;
  u3_weak  rift = u3_none;
  if (fed != u3_none && c3y == u3du(u3h(fed)) && u3h(u3h(fed)) == 2) {
    rift = u3h(u3t(u3t(fed)));
    u3k(rift);
  }

  if ( !(pir_u = _pier_init(wag_w, u3r_string(pax), rift)) ) {
    fprintf(stderr, "pier: boot: init fail\r\n");
    u3_king_bail();
    return 0;
  }

  //  XX must be called from on_lord_live
  //
  if ( c3n == _pier_boot_plan(pir_u, who, ven, pil, fed, mor) ) {
    fprintf(stderr, "pier: boot plan fail\r\n");
    //  XX dispose
    //
    u3_pier_bail(pir_u);
    exit(1);
  }

  u3z(pax);

  return pir_u;
}

/* _pier_save_cb(): save snapshot upon serf/disk synchronization.
*/
static void
_pier_save_cb(void* ptr_v, c3_d eve_d)
{
  u3_pier* pir_u = ptr_v;

#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): save: send at %" PRIu64 "\r\n", pir_u->god_u->eve_d, eve_d);
#endif

  u3_lord_save(pir_u->god_u);
}

/* u3_pier_save(): save a non-portable snapshot
*/
c3_o
u3_pier_save(u3_pier* pir_u)
{
#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): save: plan\r\n", pir_u->god_u->eve_d);
#endif
  if ( u3_psat_play == pir_u->sat_e ) {
    u3_lord_save(pir_u->god_u);
    return c3y;
  }

  if ( u3_psat_work == pir_u->sat_e ) {
    _pier_wall_plan(pir_u, 0, pir_u, _pier_save_cb);
    return c3y;
  }

  return c3n;
}

/* _pier_cram_cb(): save snapshot upon serf/disk synchronization.
*/
static void
_pier_cram_cb(void* ptr_v, c3_d eve_d)
{
  u3_pier* pir_u = ptr_v;

#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): cram: send at %" PRIu64 "\r\n", pir_u->god_u->eve_d, eve_d);
#endif

  u3_lord_cram(pir_u->god_u);
}

/* u3_pier_cram(): save a portable snapshot.
*/
c3_o
u3_pier_cram(u3_pier* pir_u)
{
#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): cram: plan\r\n", pir_u->god_u->eve_d);
#endif

  if ( u3_psat_play == pir_u->sat_e ) {
    u3_lord_cram(pir_u->god_u);
    return c3y;
  }

  if ( u3_psat_work == pir_u->sat_e ) {
    _pier_wall_plan(pir_u, 0, pir_u, _pier_cram_cb);
    return c3y;
  }

  return c3n;
}

/* u3_pier_meld(): globally deduplicate persistent state.
*/
void
u3_pier_meld(u3_pier* pir_u)
{
#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): meld: plan\r\n", pir_u->god_u->eve_d);
#endif

  u3_lord_meld(pir_u->god_u);
}

/* u3_pier_pack(): defragment persistent state.
*/
void
u3_pier_pack(u3_pier* pir_u)
{
#ifdef VERBOSE_PIER
  fprintf(stderr, "pier: (%" PRIu64 "): meld: plan\r\n", pir_u->god_u->eve_d);
#endif

  u3_lord_pack(pir_u->god_u);
}

/* _pier_work_close_cb(): dispose u3_work after closing handles.
*/
static void
_pier_work_close_cb(uv_handle_t* idl_u)
{
  u3_work* wok_u = idl_u->data;
  c3_free(wok_u);
}

/* _pier_work_close(): close drivers/handles in the u3_psat_work state.
*/
static void
_pier_work_close(u3_work* wok_u)
{
  u3_auto_exit(wok_u->car_u);

  //  free pending effects
  //
  {
    u3_gift* gif_u = wok_u->fec_u.ext_u;
    u3_gift* nex_u;

    while ( gif_u ) {
      nex_u = gif_u->nex_u;
      u3_gift_free(gif_u);
      gif_u = nex_u;
    }
  }

  uv_close((uv_handle_t*)&wok_u->pep_u, _pier_work_close_cb);
  uv_close((uv_handle_t*)&wok_u->cek_u, 0);
  uv_close((uv_handle_t*)&wok_u->idl_u, 0);
  wok_u->pep_u.data = wok_u;
}

/* _pier_done(): dispose pier.
*/
static void
_pier_free(u3_pier* pir_u)
{
  c3_free(pir_u->pax_c);

  // XX remove
  //
  c3_free(pir_u->sav_u);

  c3_free(pir_u);
}

/* _pier_done(): graceful shutdown complete, notify king.
*/
static void
_pier_done(u3_pier* pir_u)
{
  //  XX unlink properly
  //
  u3K.pir_u = 0;
  _pier_free(pir_u);
  u3_king_done();
}

/* _pier_exit(): synchronous shutdown.
*/
static void
_pier_exit(u3_pier* pir_u)
{
  u3_assert( u3_psat_done == pir_u->sat_e );

  if ( pir_u->log_u ) {
    u3_disk_exit(pir_u->log_u);
    pir_u->log_u = 0;
  }

  if ( pir_u->god_u ) {
    u3_lord_exit(pir_u->god_u);
    pir_u->god_u = 0;
  }
  else {
    //  otherwise called in _pier_on_lord_exit()
    //
    _pier_done(pir_u);
  }
}

/* _pier_work_exit(): commence graceful shutdown.
*/
static void
_pier_work_exit_cb(void* ptr_v, c3_d eve_d)
{
  u3_pier* pir_u = ptr_v;

  _pier_work_close(pir_u->wok_u);
  pir_u->wok_u = 0;

  _pier_exit(pir_u);
}

/* _pier_work_exit(): setup graceful shutdown callbacks.
*/
static void
_pier_work_exit(u3_pier* pir_u)
{
  _pier_wall_plan(pir_u, 0, pir_u, _pier_save_cb);
  _pier_wall_plan(pir_u, 0, pir_u, _pier_work_exit_cb);

  //  XX moveme, XX bails if not started
  //
  {
    c3_l cod_l = u3a_lush(c3__save);
    u3_save_io_exit(pir_u);
    u3a_lop(cod_l);
  }

  pir_u->sat_e = u3_psat_done;
}

/* u3_pier_exit(): graceful shutdown.
*/
void
u3_pier_exit(u3_pier* pir_u)
{
  switch ( pir_u->sat_e ) {
    default: {
      fprintf(stderr, "pier: unknown exit: %u\r\n", pir_u->sat_e);
      u3_assert(0);
    }

    case u3_psat_done: return;

    case u3_psat_work: _pier_work_exit(pir_u); return;

    case u3_psat_init: break;

    case u3_psat_boot: {
      //  XX properly dispose boot
      //  XX also on actual boot
      //
      c3_free(pir_u->bot_u);
      pir_u->bot_u = 0;
    } break;

    case u3_psat_play: {
      //  XX dispose play q
      //
      c3_free(pir_u->pay_u);
      pir_u->pay_u = 0;
    } break;
  }

  pir_u->sat_e = u3_psat_done;
  _pier_exit(pir_u);
}

/* u3_pier_bail(): immediately shutdown due to error.
*/
void
u3_pier_bail(u3_pier* pir_u)
{
  u3_Host.xit_i = 1;

  //  halt serf
  //
  if ( pir_u->god_u ) {
    u3_lord_halt(pir_u->god_u);
    pir_u->god_u = 0;
  }

  //  exig i/o drivers
  //
  if (  (u3_psat_work == pir_u->sat_e)
     && pir_u->wok_u )
  {
    _pier_work_close(pir_u->wok_u);
    pir_u->wok_u = 0;
  }

  //  close db
  //
  if ( pir_u->log_u ) {
    u3_disk_exit(pir_u->log_u);
    pir_u->log_u = 0;
  }

  pir_u->sat_e = u3_psat_done;

  _pier_done(pir_u);
}

/* c3_rand(): fill a 512-bit (16-word) buffer.
*/
void
c3_rand(c3_w* rad_w)
{
  if ( 0 != ent_getentropy(rad_w, 64) ) {
    fprintf(stderr, "c3_rand getentropy: %s\n", strerror(errno));
    //  XX review
    //
    u3_king_bail();
  }
}

/* _pier_dump_tape(): dump a tape, old style.  Don't do this.
*/
static void
_pier_dump_tape(FILE* fil_u, u3_noun tep)
{
  u3_noun tap = tep;

  while ( c3y == u3du(tap) ) {
    c3_c car_c;

    //  XX this utf-8 caution is unwarranted
    //
    //    we already write() utf8 directly to streams in term.c
    //
    // if ( u3h(tap) >= 127 ) {
    //   car_c = '?';
    // } else
    car_c = u3h(tap);

    putc(car_c, fil_u);
    tap = u3t(tap);
  }

  u3z(tep);
}

/* _pier_dump_wall(): dump a wall, old style.  Don't do this.
*/
static void
_pier_dump_wall(FILE* fil_u, u3_noun wol)
{
  u3_noun wal = wol;

  while ( u3_nul != wal ) {
    _pier_dump_tape(fil_u, u3k(u3h(wal)));

    wal = u3t(wal);

    if ( u3_nul != wal ) {
      putc(13, fil_u);
      putc(10, fil_u);
    }
  }

  u3z(wol);
}

/* u3_pier_tank(): dump single tank.
*/
void
u3_pier_tank(c3_l tab_l, c3_w pri_w, u3_noun tac)
{
  u3_noun blu = u3_term_get_blew(0);
  c3_l  col_l = u3h(blu);
  FILE* fil_u = u3_term_io_hija();

  //  XX temporary, for urb.py test runner
  //  XX eval --cue also using this;
  //     would be nice to have official way to dump goof to stderr
  //
  if ( c3y == u3_Host.ops_u.dem ) {
    fil_u = stderr;
  }

  if ( c3n == u3_Host.ops_u.tem ) {
    switch ( pri_w ) {
        case 3: fprintf(fil_u, "\033[31m>>> "); break;
        case 2: fprintf(fil_u, "\033[33m>>  "); break;
        case 1: fprintf(fil_u, "\033[32m>   "); break;
        case 0: fprintf(fil_u, "\033[38;5;244m"); break;
    }
  }
  else {
    switch ( pri_w ) {
        case 3: fprintf(fil_u, ">>> "); break;
        case 2: fprintf(fil_u, ">>  "); break;
        case 1: fprintf(fil_u, ">   "); break;
    }
  }

  //  if we have no arvo kernel and can't evaluate nock
  //  only print %leaf tanks
  //
  if ( 0 == u3A->roc ) {
    if ( c3__leaf == u3h(tac) ) {
      _pier_dump_tape(fil_u, u3k(u3t(tac)));
    }
  }
  //  We are calling nock here, but hopefully need no protection.
  //
  else {
    u3_noun wol = u3dc("wash", u3nc(tab_l, col_l), u3k(tac));

    _pier_dump_wall(fil_u, wol);
  }

  if ( c3n == u3_Host.ops_u.tem ) {
    fprintf(fil_u, "\033[0m");
  }

  fflush(fil_u);

  u3_term_io_loja(0, fil_u);
  u3z(blu);
  u3z(tac);
}

/* u3_pier_punt(): dump tank list.
*/
void
u3_pier_punt(c3_l tab_l, u3_noun tac)
{
  u3_noun cat = tac;

  while ( c3y == u3du(cat) ) {
    u3_pier_tank(tab_l, 0, u3k(u3h(cat)));
    cat = u3t(cat);
  }

  u3z(tac);
}

/* u3_pier_punt_goof(): dump a [mote tang] crash report.
*/
void
u3_pier_punt_goof(const c3_c* cap_c, u3_noun dud)
{
  u3_noun bud = dud;
  u3_noun mot, tan;

  u3x_cell(dud, &mot, &tan);

  u3l_log("");
  u3_pier_punt(0, u3qb_flop(tan));

  {
    c3_c* mot_c = u3r_string(mot);
    u3l_log("%s: bail: %%%s", cap_c, mot_c);
    c3_free(mot_c);
  }

  u3z(bud);
}

/* u3_pier_punt_ovum(): print ovum details.
*/
void
u3_pier_punt_ovum(const c3_c* cap_c, u3_noun wir, u3_noun tag)
{
  c3_c* tag_c = u3r_string(tag);
  u3_noun riw = u3do("spat", wir);
  c3_c* wir_c = u3r_string(riw);

  u3l_log("%s: %%%s event on %s failed\n", cap_c, tag_c, wir_c);

  c3_free(tag_c);
  c3_free(wir_c);
  u3z(riw);
}

/* u3_pier_sway(): print trace.
*/
void
u3_pier_sway(c3_l tab_l, u3_noun tax)
{
  u3_noun mok = u3dc("mook", 2, tax);

  u3_pier_punt(tab_l, u3k(u3t(mok)));
  u3z(mok);
}

/* u3_pier_mark(): mark all Loom allocations in all u3_pier structs.
*/
c3_w
u3_pier_mark(FILE* fil_u)
{
  return 0;
}
