/*-
 * Copyright (c) 1998 Brian Somers <brian@Awfulhak.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Id: mp.c,v 1.1.2.15 1998/04/25 00:09:21 brian Exp $
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <net/if_dl.h>
#include <sys/socket.h>

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "command.h"
#include "mbuf.h"
#include "log.h"
#include "defs.h"
#include "timer.h"
#include "fsm.h"
#include "iplist.h"
#include "throughput.h"
#include "slcompress.h"
#include "ipcp.h"
#include "auth.h"
#include "lcp.h"
#include "lqr.h"
#include "hdlc.h"
#include "async.h"
#include "ccp.h"
#include "link.h"
#include "descriptor.h"
#include "physical.h"
#include "chat.h"
#include "lcpproto.h"
#include "filter.h"
#include "mp.h"
#include "chap.h"
#include "datalink.h"
#include "bundle.h"
#include "ip.h"
#include "prompt.h"
#include "id.h"
#include "arp.h"

void
peerid_Init(struct peerid *peer)
{
  peer->enddisc.class = 0;
  *peer->enddisc.address = '\0';
  peer->enddisc.len = 0;
  *peer->authname = '\0';
}

int
peerid_Equal(const struct peerid *p1, const struct peerid *p2)
{
  return !strcmp(p1->authname, p2->authname) &&
         p1->enddisc.class == p2->enddisc.class &&
         p1->enddisc.len == p2->enddisc.len &&
         !memcmp(p1->enddisc.address, p2->enddisc.address, p1->enddisc.len);
}

static u_int32_t
inc_seq(struct mp *mp, u_int32_t seq)
{
  seq++;
  if (mp->peer_is12bit) {
    if (seq & 0xfffff000)
      seq = 0;
  } else if (seq & 0xff000000)
    seq = 0;
  return seq;
}

static int
mp_ReadHeader(struct mp *mp, struct mbuf *m, struct mp_header *header)
{
  if (mp->local_is12bit) {
    header->seq = ntohs(*(u_int16_t *)MBUF_CTOP(m));
    if (header->seq & 0x3000) {
      LogPrintf(LogWARN, "Oops - MP header without required zero bits\n");
      return 0;
    }
    header->begin = header->seq & 0x8000 ? 1 : 0;
    header->end = header->seq & 0x4000 ? 1 : 0;
    header->seq &= 0x0fff;
    return 2;
  } else {
    header->seq = ntohl(*(u_int32_t *)MBUF_CTOP(m));
    if (header->seq & 0x3f000000) {
      LogPrintf(LogWARN, "Oops - MP header without required zero bits\n");
      return 0;
    }
    header->begin = header->seq & 0x80000000 ? 1 : 0;
    header->end = header->seq & 0x40000000 ? 1 : 0;
    header->seq &= 0x00ffffff;
    return 4;
  }
}

static void
mp_LayerStart(void *v, struct fsm *fp)
{
  /* The given FSM (ccp) is about to start up ! */
}

static void
mp_LayerUp(void *v, struct fsm *fp)
{
  /* The given fsm (ccp) is now up */
}

static void
mp_LayerDown(void *v, struct fsm *fp)
{
  /* The given FSM (ccp) has been told to come down */
}

static void
mp_LayerFinish(void *v, struct fsm *fp)
{
  /* The given fsm (ccp) is now down */
}

void
mp_Init(struct mp *mp, struct bundle *bundle)
{
  mp->peer_is12bit = mp->local_is12bit = 0;
  mp->peer_mrru = mp->local_mrru = 0;

  peerid_Init(&mp->peer);

  mp->seq.out = 0;
  mp->seq.min_in = 0;
  mp->seq.next_in = 0;
  mp->inbufs = NULL;
  mp->bundle = bundle;

  mp->link.type = MP_LINK;
  mp->link.name = "mp";
  mp->link.len = sizeof *mp;

  throughput_init(&mp->link.throughput);
  memset(mp->link.Queue, '\0', sizeof mp->link.Queue);
  memset(mp->link.proto_in, '\0', sizeof mp->link.proto_in);
  memset(mp->link.proto_out, '\0', sizeof mp->link.proto_out);

  mp->fsmp.LayerStart = mp_LayerStart;
  mp->fsmp.LayerUp = mp_LayerUp;
  mp->fsmp.LayerDown = mp_LayerDown;
  mp->fsmp.LayerFinish = mp_LayerFinish;
  mp->fsmp.object = mp;

  mp->cfg.mrru = 0;
  mp->cfg.shortseq = NEG_ENABLED|NEG_ACCEPTED;
  mp->cfg.enddisc.class = 0;
  *mp->cfg.enddisc.address = '\0';
  mp->cfg.enddisc.len = 0;

  lcp_Init(&mp->link.lcp, mp->bundle, &mp->link, NULL);
  ccp_Init(&mp->link.ccp, mp->bundle, &mp->link, &mp->fsmp);
}

int
mp_Up(struct mp *mp, const char *name, const struct peerid *peer,
      u_short local_mrru, u_short peer_mrru, int local_shortseq,
      int peer_shortseq)
{
  if (mp->active) {
    /* We're adding a link - do a last validation on our parameters */
    if (!peerid_Equal(peer, &mp->peer)) {
      LogPrintf(LogPHASE, "%s: Inappropriate peer !\n", name);
      return 0;
    }
    if (mp->local_mrru != local_mrru ||
        mp->peer_mrru != peer_mrru ||
        mp->local_is12bit != local_shortseq ||
        mp->peer_is12bit != peer_shortseq) {
      LogPrintf(LogPHASE, "%s: Invalid MRRU/SHORTSEQ MP parameters !\n", name);
      return 0;
    }
  } else {
    /* First link in multilink mode */

    mp->local_mrru = local_mrru;
    mp->peer_mrru = peer_mrru;
    mp->local_is12bit = local_shortseq;
    mp->peer_is12bit = peer_shortseq;
    mp->peer = *peer;

    throughput_init(&mp->link.throughput);
    memset(mp->link.Queue, '\0', sizeof mp->link.Queue);
    memset(mp->link.proto_in, '\0', sizeof mp->link.proto_in);
    memset(mp->link.proto_out, '\0', sizeof mp->link.proto_out);

    mp->seq.out = 0;
    mp->seq.min_in = 0;
    mp->seq.next_in = 0;

    /* Re-point our IPCP layer at our MP link */
    ipcp_SetLink(&mp->bundle->ncp.ipcp, &mp->link);

    /* Our lcp's already up 'cos of the NULL parent */
    FsmUp(&mp->link.ccp.fsm);
    FsmOpen(&mp->link.ccp.fsm);

    mp->active = 1;
  }

  return 1;
}

void
mp_Down(struct mp *mp)
{
  if (mp->active) {
    struct mbuf *next;

    /* CCP goes down with a bank */
    FsmDown(&mp->link.ccp.fsm);
    FsmClose(&mp->link.ccp.fsm);

    /* Received fragments go in the bit-bucket */
    while (mp->inbufs) {
      next = mp->inbufs->pnext;
      pfree(mp->inbufs);
      mp->inbufs = next;
    }

    peerid_Init(&mp->peer);
    mp->active = 0;
  }
}

void
mp_linkInit(struct mp_link *mplink)
{
  mplink->seq = 0;
  mplink->weight = 1500;
}

void
mp_Input(struct mp *mp, struct mbuf *m, struct physical *p)
{
  struct mp_header mh, h;
  struct mbuf *q, *last;
  int32_t seq;

  if (mp_ReadHeader(mp, m, &mh) == 0) {
    pfree(m);
    return;
  }

  seq = p->dl->mp.seq;
  p->dl->mp.seq = mh.seq;
  if (mp->seq.min_in == seq) {
    /*
     * We've received new data on the link that has our min (oldest) seq.
     * Figure out which link now has the smallest (oldest) seq.
     */
    struct datalink *dl;

    mp->seq.min_in = p->dl->mp.seq;
    for (dl = mp->bundle->links; dl; dl = dl->next)
      if (mp->seq.min_in > dl->mp.seq)
        mp->seq.min_in = dl->mp.seq;
  }

  /*
   * Now process as many of our fragments as we can, adding our new
   * fragment in as we go, and ordering with the oldest at the top of
   * the queue.
   */

  if (!mp->inbufs) {
    mp->inbufs = m;
    m = NULL;
  }

  last = NULL;
  seq = mp->seq.next_in;
  q = mp->inbufs;
  while (q) {
    mp_ReadHeader(mp, q, &h);
    if (m && h.seq > mh.seq) {
      /* Our received fragment fits in before this one, so link it in */
      if (last)
        last->pnext = m;
      else
        mp->inbufs = m;
      m->pnext = q;
      q = m;
      h = mh;
      m = NULL;
    }

    if (h.seq != seq) {
      /* we're missing something :-( */
      if (mp->seq.min_in > seq) {
        /* we're never gonna get it */
        struct mbuf *next;

        /* Zap all older fragments */
        while (mp->inbufs != q) {
          LogPrintf(LogDEBUG, "Drop frag\n");
          next = mp->inbufs->pnext;
          pfree(mp->inbufs);
          mp->inbufs = next;
        }

        /*
         * Zap everything until the next `end' fragment OR just before
         * the next `begin' fragment OR 'till seq.min_in - whichever
         * comes first.
         */
        do {
          mp_ReadHeader(mp, mp->inbufs, &h);
          if (h.begin) {
            /* We might be able to process this ! */
            h.seq--;  /* We're gonna look for fragment with h.seq+1 */
            break;
          }
          next = mp->inbufs->pnext;
          LogPrintf(LogDEBUG, "Drop frag %u\n", h.seq);
          pfree(mp->inbufs);
          mp->inbufs = next;
        } while (mp->inbufs && (h.seq >= mp->seq.min_in || h.end));

        /*
         * Continue processing things from here.
         * This deals with the possibility that we received a fragment
         * on the slowest link that invalidates some of our data (because
         * of the hole at `q'), but where there are subsequent `whole'
         * packets that have already been received.
         */

        mp->seq.next_in = seq = h.seq + 1;
        last = NULL;
        q = mp->inbufs;
      } else
        /* we may still receive the missing fragment */
        break;
    } else if (h.end) {
      /* We've got something, reassemble */
      struct mbuf **frag = &q;
      int len;
      u_long first = -1;

      do {
        *frag = mp->inbufs;
        mp->inbufs = mp->inbufs->pnext;
        len = mp_ReadHeader(mp, *frag, &h);
        if (first == -1)
          first = h.seq;
        (*frag)->offset += len;
        (*frag)->cnt -= len;
        (*frag)->pnext = NULL;
        if (frag == &q && !h.begin) {
          LogPrintf(LogWARN, "Oops - MP frag %lu should have a begin flag\n",
                    (u_long)h.seq);
          pfree(q);
          q = NULL;
        } else if (frag != &q && h.begin) {
          LogPrintf(LogWARN, "Oops - MP frag %lu should have an end flag\n",
                    (u_long)h.seq - 1);
          /*
           * Stuff our fragment back at the front of the queue and zap
           * our half-assembed packet.
           */
          (*frag)->pnext = mp->inbufs;
          mp->inbufs = *frag;
          *frag = NULL;
          pfree(q);
          q = NULL;
          frag = &q;
          h.end = 0;	/* just in case it's a whole packet */
        } else
          do
            frag = &(*frag)->next;
          while (*frag != NULL);
      } while (!h.end);

      if (q) {
        u_short proto;
        u_char ch;

        q = mbread(q, &ch, 1);
        proto = ch;
        if (!(proto & 1)) {
          q = mbread(q, &ch, 1);
          proto <<= 8;
          proto += ch;
        }
        if (LogIsKept(LogDEBUG))
          LogPrintf(LogDEBUG, "MP: Reassembled frags %ld-%lu, length %d\n",
                    first, (u_long)h.seq, plength(q));
        hdlc_DecodePacket(mp->bundle, proto, q, &mp->link);
      }

      mp->seq.next_in = seq = h.seq + 1;
      last = NULL;
      q = mp->inbufs;
    } else {
      /* Look for the next fragment */
      seq++;
      last = q;
      q = q->pnext;
    }
  }

  if (m) {
    /* We still have to find a home for our new fragment */
    last = NULL;
    for (q = mp->inbufs; q; last = q, q = q->pnext) {
      mp_ReadHeader(mp, q, &h);
      if (h.seq > mh.seq) {
        /* Our received fragment fits in before this one, so link it in */
        if (last)
          last->pnext = m;
        else
          mp->inbufs = m;
        m->pnext = q;
        break;
      }
    }
  }
}

static void
mp_Output(struct mp *mp, struct link *l, struct mbuf *m, int begin, int end)
{
  struct mbuf *mo;

  /* Stuff an MP header on the front of our packet and send it */
  mo = mballoc(4, MB_MP);
  mo->next = m;
  if (mp->peer_is12bit) {
    u_int16_t *seq16;

    seq16 = (u_int16_t *)MBUF_CTOP(mo);
    *seq16 = htons((begin << 15) | (end << 14) | (u_int16_t)mp->seq.out);
    mo->cnt = 2;
  } else {
    u_int32_t *seq32;

    seq32 = (u_int32_t *)MBUF_CTOP(mo);
    *seq32 = htonl((begin << 31) | (end << 30) | (u_int32_t)mp->seq.out);
    mo->cnt = 4;
  }
  if (LogIsKept(LogDEBUG))
    LogPrintf(LogDEBUG, "MP[frag %d]: Send %d bytes on %s\n",
              mp->seq.out, plength(mo), l->name);
  mp->seq.out = inc_seq(mp, mp->seq.out);

  HdlcOutput(l, PRI_NORMAL, PROTO_MP, mo);
}

int
mp_FillQueues(struct bundle *bundle)
{
  struct mp *mp = &bundle->ncp.mp;
  struct datalink *dl;
  int total, add, len, begin, end, looped;
  struct mbuf *m, *mo;

  /*
   * XXX:  This routine is fairly simplistic.  It should re-order the
   *       links based on the amount of data less than the links weight
   *       that was queued.  That way we'd ``prefer'' the least used
   *       links the next time 'round.
   */

  total = 0;
  for (dl = bundle->links; dl; dl = dl->next) {
    if (dl->physical->out)
      /* this link has suffered a short write.  Let it continue */
      continue;
    add = link_QueueLen(&dl->physical->link);
    total += add;
    if (add)
      /* this link has got stuff already queued.  Let it continue */
      continue;
    if (!link_QueueLen(&mp->link) && !IpFlushPacket(&mp->link, bundle))
      /* Nothing else to send */
      break;

    m = link_Dequeue(&mp->link);
    len = plength(m);
    add += len;
    begin = 1;
    end = 0;
    looped = 0;

    for (; !end; dl = dl->next) {
      if (dl == NULL) {
        /* Keep going 'till we get rid of the whole of `m' */
        looped = 1;
        dl = bundle->links;
      }
      if (len <= dl->mp.weight + LINK_MINWEIGHT) {
        mo = m;
        end = 1;
      } else {
        mo = mballoc(dl->mp.weight, MB_MP);
        mo->cnt = dl->mp.weight;
        len -= mo->cnt;
        m = mbread(m, MBUF_CTOP(mo), mo->cnt);
      }
      mp_Output(mp, &dl->physical->link, mo, begin, end);
      begin = 0;
    }
    if (!dl || looped)
      break;
  }

  return total;
}

int
mp_SetDatalinkWeight(struct cmdargs const *arg)
{
  int val;

  if (arg->argc != arg->argn+1)
    return -1;
  
  val = atoi(arg->argv[arg->argn]);
  if (val < LINK_MINWEIGHT) {
    LogPrintf(LogWARN, "Link weights must not be less than %d\n",
              LINK_MINWEIGHT);
    return 1;
  }
  arg->cx->mp.weight = val;
  return 0;
}

int
mp_ShowStatus(struct cmdargs const *arg)
{
  struct mp *mp = &arg->bundle->ncp.mp;

  prompt_Printf(arg->prompt, "Multilink is %sactive\n", mp->active ? "" : "in");

  prompt_Printf(arg->prompt, "\nMy Side:\n");
  if (mp->active) {
    prompt_Printf(arg->prompt, " MRRU:          %u\n", mp->local_mrru);
    prompt_Printf(arg->prompt, " Short Seq:     %s\n",
                  mp->local_is12bit ? "on" : "off");
  }
  prompt_Printf(arg->prompt, " Discriminator: %s\n",
                mp_Enddisc(mp->cfg.enddisc.class, mp->cfg.enddisc.address,
                           mp->cfg.enddisc.len));

  prompt_Printf(arg->prompt, "\nHis Side:\n");
  if (mp->active) {
    prompt_Printf(arg->prompt, " Auth Name:     %s\n", mp->peer.authname);
    prompt_Printf(arg->prompt, " Next SEQ:      %u\n", mp->seq.out);
    prompt_Printf(arg->prompt, " MRRU:          %u\n", mp->peer_mrru);
    prompt_Printf(arg->prompt, " Short Seq:     %s\n",
                  mp->peer_is12bit ? "on" : "off");
  }
  prompt_Printf(arg->prompt,   " Discriminator: %s\n",
                mp_Enddisc(mp->peer.enddisc.class, mp->peer.enddisc.address,
                           mp->peer.enddisc.len));

  prompt_Printf(arg->prompt, "\nDefaults:\n");
  
  prompt_Printf(arg->prompt, " MRRU:          ");
  if (mp->cfg.mrru)
    prompt_Printf(arg->prompt, "%d (multilink enabled)\n", mp->cfg.mrru);
  else
    prompt_Printf(arg->prompt, "disabled\n");
  prompt_Printf(arg->prompt, " Short Seq:     %s\n",
                  command_ShowNegval(mp->cfg.shortseq));

  return 0;
}

const char *
mp_Enddisc(u_char c, const char *address, int len)
{
  static char result[100];
  int f, header;

  switch (c) {
    case ENDDISC_NULL:
      sprintf(result, "Null Class");
      break;

    case ENDDISC_LOCAL:
      snprintf(result, sizeof result, "Local Addr: %.*s", len, address);
      break;

    case ENDDISC_IP:
      if (len == 4)
        snprintf(result, sizeof result, "IP %s",
                 inet_ntoa(*(const struct in_addr *)address));
      else
        sprintf(result, "IP[%d] ???", len);
      break;

    case ENDDISC_MAC:
      if (len == 6) {
        const u_char *m = (const u_char *)address;
        snprintf(result, sizeof result, "MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 m[0], m[1], m[2], m[3], m[4], m[5]);
      } else
        sprintf(result, "MAC[%d] ???", len);
      break;

    case ENDDISC_MAGIC:
      sprintf(result, "Magic: 0x");
      header = strlen(result);
      if (len > sizeof result - header - 1)
        len = sizeof result - header - 1;
      for (f = 0; f < len; f++)
        sprintf(result + header + 2 * f, "%02x", address[f]);
      break;

    case ENDDISC_PSN:
      snprintf(result, sizeof result, "PSN: %.*s", len, address);
      break;

     default:
      sprintf(result, "%d: ", (int)c);
      header = strlen(result);
      if (len > sizeof result - header - 1)
        len = sizeof result - header - 1;
      for (f = 0; f < len; f++)
        sprintf(result + header + 2 * f, "%02x", address[f]);
      break;
  }
  return result;
}

int
mp_SetEnddisc(struct cmdargs const *arg)
{
  struct mp *mp = &arg->bundle->ncp.mp;
  struct in_addr addr;

  if (bundle_Phase(arg->bundle) != PHASE_DEAD) {
    LogPrintf(LogWARN, "set enddisc: Only available at phase DEAD\n");
    return 1;
  }

  if (arg->argc == arg->argn) {
    mp->cfg.enddisc.class = 0;
    *mp->cfg.enddisc.address = '\0';
    mp->cfg.enddisc.len = 0;
  } else if (arg->argc > arg->argn) {
    if (!strcasecmp(arg->argv[arg->argn], "label")) {
      mp->cfg.enddisc.class = ENDDISC_LOCAL;
      strcpy(mp->cfg.enddisc.address, arg->bundle->cfg.label);
      mp->cfg.enddisc.len = strlen(mp->cfg.enddisc.address);
    } else if (!strcasecmp(arg->argv[arg->argn], "ip")) {
      if (arg->bundle->ncp.ipcp.my_ip.s_addr == INADDR_ANY)
        addr = arg->bundle->ncp.ipcp.cfg.my_range.ipaddr;
      else
        addr = arg->bundle->ncp.ipcp.my_ip;
      memcpy(mp->cfg.enddisc.address, &addr.s_addr, sizeof addr.s_addr);
      mp->cfg.enddisc.class = ENDDISC_IP;
      mp->cfg.enddisc.len = sizeof arg->bundle->ncp.ipcp.my_ip.s_addr;
    } else if (!strcasecmp(arg->argv[arg->argn], "mac")) {
      struct sockaddr_dl hwaddr;
      int s;

      if (arg->bundle->ncp.ipcp.my_ip.s_addr == INADDR_ANY)
        addr = arg->bundle->ncp.ipcp.cfg.my_range.ipaddr;
      else
        addr = arg->bundle->ncp.ipcp.my_ip;

      s = ID0socket(AF_INET, SOCK_DGRAM, 0);
      if (s < 0) {
        LogPrintf(LogERROR, "set enddisc: socket(): %s\n", strerror(errno));
        return 2;
      }
      if (get_ether_addr(s, addr, &hwaddr)) {
        mp->cfg.enddisc.class = ENDDISC_MAC;
        memcpy(mp->cfg.enddisc.address, hwaddr.sdl_data + hwaddr.sdl_nlen,
               hwaddr.sdl_alen);
        mp->cfg.enddisc.len = hwaddr.sdl_alen;
      } else {
        LogPrintf(LogWARN, "set enddisc: Can't locate MAC address for %s\n",
                  inet_ntoa(addr));
        close(s);
        return 4;
      }
      close(s);
    } else if (!strcasecmp(arg->argv[arg->argn], "magic")) {
      int f;

      randinit();
      for (f = 0; f < 20; f += sizeof(long))
        *(long *)(mp->cfg.enddisc.address + f) = random();
      mp->cfg.enddisc.class = ENDDISC_MAGIC;
      mp->cfg.enddisc.len = 20;
    } else if (!strcasecmp(arg->argv[arg->argn], "psn")) {
      if (arg->argc > arg->argn+1) {
        mp->cfg.enddisc.class = ENDDISC_PSN;
        strcpy(mp->cfg.enddisc.address, arg->argv[arg->argn+1]);
        mp->cfg.enddisc.len = strlen(mp->cfg.enddisc.address);
      } else {
        LogPrintf(LogWARN, "PSN endpoint requires additional data\n");
        return 5;
      }
    } else {
      LogPrintf(LogWARN, "%s: Unrecognised endpoint type\n",
                arg->argv[arg->argn]);
      return 6;
    }
  }

  return 0;
}
