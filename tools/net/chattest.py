#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - test tools for the GameSpy lobby chat (IRC) of Soldier of Fortune.
# Implements: SPEC-204 section 5 (test harness, no game code)
#
# Three modes, all listening only on 127.0.0.1:
#   proxy  logging relay: a game connects to 127.0.0.1:<port>, the relay forwards to a real chat server and
#          writes every line of both directions with a time stamp ("C>" = game to server, "S>" = server to game)
#   fake   scripted IRC server: answers registration, LIST, JOIN, MODE, NAMES, TOPIC, WHOIS and plays scripted
#          events when the game says a trigger word in a channel (gsc_talk <ch> ev_msgs|ev_people|ev_kickme|
#          ev_error|ev_close|ev_ping); special nicks and channels give the error paths (see FAKE_* below)
#   peer   a second user on a real chat server: joins a channel, waits for the target nick, then sends channel
#          messages, an action, a notice, private messages, a CTCP PING, parts/rejoins and changes its nick;
#          logs everything it receives (proves that the game's own messages reach the real server)
#
#   python chattest.py proxy --port 16667 --upstream peerchat.openspy.net:6667 --log trace.txt
#   python chattest.py fake --port 16668 --log fake.txt
#   python chattest.py peer --server peerchat.openspy.net:6667 --nick OMpeer --channel omtest --target OMgame
import argparse
import socket
import sys
import threading
import time

# fake server: nick and channel names that select the error paths
FAKE_TAKEN = 'taken'      # nick prefix -> 433
FAKE_ERRNICK = 'errnick'  # nick prefix -> ERROR before the welcome
FAKE_CLOSENICK = 'closenick'  # nick prefix -> connection closed before the welcome
FAKE_ERRKEEP = 'errkeep'  # nick prefix -> ERROR before the welcome, socket kept open, PING 2 s later
# other special nicks: slowlist* (LIST never answered), partlist* (321 + two 322, no 323), latelist* (321 + 322,
# another 322 25 s later, no 323); channels: #full #banned #keyed (key "secret") #silent #nomode #busy (channel
# events between the join's names and its mode reply) #modes #opchan #other (own topic) #nosuch #slownames
# #slowtopic #notopic
FAKE_CHANNELS = [('#lobby', 3, 'The lobby'), ('#sof', 1, 'Soldier of Fortune'), ('#Zeta', 0, ''),
                 ('*', 2, 'secret'), ('#alpha', 5, 'first')]
FAKE_TOPIC = 'Welcome to the test channel'


class Log:
    def __init__(self, path):
        self.f = open(path, 'a', encoding='latin-1', errors='backslashreplace', newline='\n') if path else None
        self.t0 = time.time()
        self.lock = threading.Lock()

    def __call__(self, tag, text):
        line = '%8.3f %s %s' % (time.time() - self.t0, tag, text)
        with self.lock:
            print(line.encode('ascii', 'backslashreplace').decode('ascii'), flush=True)
            if self.f:
                self.f.write(line + '\n')
                self.f.flush()


def show(b):
    """bytes -> printable text, control bytes as ^<n>"""
    out = []
    for c in b:
        if c < 32 or c == 127:
            out.append('^%d' % c)
        else:
            out.append(chr(c))
    return ''.join(out)


def split_lines(buf):
    """complete lines ending in CR, LF or both; returns (lines, rest)"""
    lines = []
    start = i = 0
    while i < len(buf):
        if buf[i] in (10, 13):
            lines.append(buf[start:i])
            if buf[i] == 13 and i + 1 < len(buf) and buf[i + 1] == 10:
                i += 1
            start = i + 1
        i += 1
    return [l for l in lines if l], buf[start:]


def listener():
    """TCP socket for 127.0.0.1 listeners. On Windows SO_REUSEADDR would let a second listener bind the same port
    (a test run then talks to a stale helper), so the port is taken exclusively there."""
    ls = socket.socket()
    if sys.platform == 'win32':
        ls.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    else:
        ls.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    return ls


# ---------------------------------------------------------------- proxy

def proxy(args):
    log = Log(args.log)
    host, port = args.upstream.rsplit(':', 1)
    ls = listener()
    ls.bind(('127.0.0.1', args.port))
    ls.listen(4)
    log('--', 'proxy 127.0.0.1:%d -> %s' % (args.port, args.upstream))
    end = time.time() + args.timeout
    ls.settimeout(1.0)
    while time.time() < end:
        try:
            c, a = ls.accept()
        except socket.timeout:
            continue
        log('--', 'game connected from %s:%d' % a)
        try:
            u = socket.create_connection((host, int(port)), timeout=15)
        except OSError as e:
            log('--', 'upstream connect failed: %s' % e)
            c.close()
            continue
        u.settimeout(None)  # the 15 s was for the connect only: a quiet server must not end the relay
        log('--', 'upstream connected %s' % (u.getpeername(),))

        def pump(src, dst, tag):
            buf = b''
            while True:
                try:
                    d = src.recv(4096)
                except OSError:
                    d = b''
                if not d:
                    log('--', '%s closed' % ('game' if tag == 'C>' else 'server'))
                    try:
                        dst.shutdown(socket.SHUT_WR)
                    except OSError:
                        pass
                    return
                buf += d
                lines, buf = split_lines(buf)
                for l in lines:
                    if not (args.quiet_list and tag == 'S>' and b' 322 ' in l):
                        log(tag, show(l))
                try:
                    dst.sendall(d)
                except OSError:
                    return

        t1 = threading.Thread(target=pump, args=(c, u, 'C>'), daemon=True)
        t2 = threading.Thread(target=pump, args=(u, c, 'S>'), daemon=True)
        t1.start()
        t2.start()
        while (t1.is_alive() or t2.is_alive()) and time.time() < end:
            time.sleep(0.2)
        c.close()
        u.close()
        if args.once:
            break
    ls.close()


# ---------------------------------------------------------------- fake server

class FakeClient:
    def __init__(self, sock, log, args):
        self.s = sock
        self.log = log
        self.args = args
        self.nick = '*'
        self.user = None
        self.registered = False
        self.channels = {}   # name -> topic
        self.timed = []      # (time, line): lines sent later by tick()

    def tick(self):
        now = time.time()
        for t, line in [x for x in self.timed if x[0] <= now]:
            self.timed.remove((t, line))
            self.send(line)

    def later(self, seconds, line):
        self.timed.append((time.time() + seconds, line))

    def send(self, text):
        self.log('S>', show(text.encode('latin-1')))
        self.s.sendall(text.encode('latin-1') + b'\r\n')

    def num(self, n, rest):
        self.send(':fake.server %03d %s %s' % (n, self.nick, rest))

    def me(self):
        return '%s!%s@127.0.0.1' % (self.nick, self.user or 'u')

    def welcome(self):
        if self.nick.lower().startswith(FAKE_TAKEN):
            self.num(433, '%s :Nickname is already in use' % self.nick)
            return
        if self.nick == '1' or not self.nick:
            self.num(432, '%s :Erroneous Nickname' % self.nick)
            return
        if self.nick.lower().startswith(FAKE_ERRNICK):
            self.send('ERROR :Closing Link: %s (test refusal)' % self.nick)
            return
        if self.nick.lower().startswith(FAKE_ERRKEEP):
            # SPEC-AMEND-006 I2d.1: ERROR, the socket stays open, a PING follows 2 s later
            self.send('ERROR :x')
            self.later(2.0, 'PING :abc')
            return
        if self.nick.lower().startswith(FAKE_CLOSENICK):
            self.s.close()
            raise ConnectionError('closed before welcome')
        self.registered = True
        self.num(1, ':Welcome to the fake chat %s' % self.nick)
        self.num(2, ':Your host is fake.server')
        self.num(375, ':- fake.server Message of the day -')
        self.num(372, ':- test')
        self.num(376, ':End of MOTD')
        self.send('PING :fake123')

    def names(self, ch):
        if ch.lower() == '#opchan':
            self.num(353, '= %s :@%s +voiceguy bob' % (ch, self.nick))
            self.num(366, '%s :End of /NAMES list.' % ch)
            return
        self.num(353, '= %s :%s @opguy +voiceguy bob Zed' % (ch, self.nick))
        self.num(366, '%s :End of /NAMES list.' % ch)

    def event(self, ch, word):
        n = self.nick
        ev = []
        if word == 'ev_msgs':
            ev = [':bob!b@h.example PRIVMSG %s :hello from bob' % ch,
                  ':bob!b@h.example PRIVMSG %s :\x01ACTION waves at everyone\x01' % ch,
                  ':bob!b@h.example NOTICE %s :channel notice from bob' % ch,
                  ':bob!b@h.example PRIVMSG %s :colour \x07test\x02 stripped?' % ch,
                  ':bob!b@h.example UTM %s :utm text from bob' % ch,
                  ':bob!b@h.example PRIVMSG %s :private hello' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01ACTION private action\x01' % n,
                  ':bob!b@h.example NOTICE %s :private notice' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01PING 12345\x01' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01VERSION\x01' % n,
                  'PING :fake456']
        elif word == 'ev_people':
            ev = [':carl!c@h.example JOIN %s' % ch,
                  ':carl!c@h.example PART %s :bye' % ch,
                  ':dave!d@h.example JOIN :%s' % ch,
                  ':opguy!o@h.example MODE %s +o dave' % ch,
                  ':opguy!o@h.example MODE %s +v dave' % ch,
                  ':opguy!o@h.example MODE %s -o dave' % ch,
                  ':opguy!o@h.example MODE %s -v dave' % ch,
                  ':opguy!o@h.example MODE %s +m' % ch,
                  ':dave!d@h.example NICK :dave2',
                  ':opguy!o@h.example TOPIC %s :A new topic set by opguy' % ch,
                  ':opguy!o@h.example KICK %s dave2 :behave' % ch,
                  ':bob!b@h.example QUIT :Quit: gone']
        elif word == 'ev_kickme':
            ev = [':opguy!o@h.example KICK %s %s :test kick' % (ch, n)]
            self.channels.pop(ch.lower(), None)
        elif word == 'ev_ping':
            ev = ['PING :fake789']
        elif word == 'ev_ctcp':
            # SPEC-AMEND-006 I2d.2: which texts are CTCP requests (to the own nick and to the channel)
            ev = [':bob!b@h.example PRIVMSG %s :\x01VERSION\x01' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01VERSION 1\x01' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01PING\x01' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01PING 7\x01' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01ping x\x01' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01ACTION\x01' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01action lower\x01' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01ACTION private act\x01' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01ACTION no end' % n,
                  ':bob!b@h.example PRIVMSG %s :\x011VERSION x\x01' % n,
                  ':bob!b@h.example NOTICE %s :\x01PING 8\x01' % n,
                  ':bob!b@h.example PRIVMSG %s :\x01VERSION 2\x01' % ch,
                  ':bob!b@h.example PRIVMSG %s :\x01ACTION chan act\x01' % ch,
                  ':bob!b@h.example PRIVMSG %s :\x01PING 9\x01' % ch,
                  ':bob!b@h.example PRIVMSG %s :ctcp-end' % ch]
        elif word == 'ev_other':
            # SPEC-AMEND-006 I2d.6: events of a channel the client is not in, and a message to another nick
            ev = [':bob!b@h.example PRIVMSG #elsewhere :hi from elsewhere',
                  ':bob!b@h.example NOTICE #elsewhere :notice from elsewhere',
                  ':carl!c@h.example JOIN #elsewhere',
                  ':opguy!o@h.example TOPIC #elsewhere :elsewhere topic',
                  ':opguy!o@h.example KICK #elsewhere %s :kicked elsewhere' % n,
                  ':bob!b@h.example PRIVMSG somebodyelse :not for you',
                  ':bob!b@h.example PRIVMSG %s :other-end' % ch]
        elif word == 'ev_error':
            self.send('ERROR :Closing Link: test error')
            time.sleep(0.2)
            self.s.close()
            raise ConnectionError('error sent')
        elif word == 'ev_close':
            self.s.close()
            raise ConnectionError('closed by test')
        for e in ev:
            self.send(e)
            time.sleep(0.05)

    def line(self, raw):
        t = raw.decode('latin-1')
        prefix = ''
        if t.startswith(':'):
            prefix, _, t = t.partition(' ')
        trailing = None
        if ' :' in t:
            t, _, trailing = t.partition(' :')
        parts = t.split()
        if not parts:
            return
        cmd = parts[0].upper()
        p = parts[1:]
        if trailing is not None:
            p.append(trailing)
        if cmd == 'NICK':
            self.nick = p[0] if p else ''
            if self.user is not None and not self.registered:
                self.welcome()
        elif cmd == 'USER':
            self.user = p[0] if p else ''
            if self.nick != '*' and not self.registered:
                self.welcome()
        elif cmd == 'PONG':
            pass
        elif cmd == 'QUIT':
            self.send('ERROR :Closing Link: %s (Quit: %s)' % (self.nick, p[0] if p else ''))
            self.s.close()
            raise ConnectionError('quit')
        elif not self.registered:
            self.num(451, ':You have not registered')
        elif cmd == 'LIST' and self.nick.lower().startswith('slowlist'):
            pass  # no answer: the client's 30 s limit
        elif cmd == 'LIST' and self.nick.lower().startswith('partlist'):
            self.num(321, 'Channel :Users Name')   # two channels, no 323: the 30 s limit after a partial list
            for name, users, topic in FAKE_CHANNELS[:2]:
                self.num(322, '%s %d :%s' % (name, users, topic))
        elif cmd == 'LIST' and self.nick.lower().startswith('latelist'):
            # SPEC-AMEND-006 I2d.4 rule 3: a reply 25 s later restarts the request's 30 s
            self.num(321, 'Channel :Users Name')
            self.num(322, '%s %d :%s' % FAKE_CHANNELS[0])
            self.later(25.0, ':fake.server 322 %s %s %d :%s' % ((self.nick,) + FAKE_CHANNELS[1]))
        elif cmd == 'LIST':
            self.num(321, 'Channel :Users Name')
            for name, users, topic in FAKE_CHANNELS:
                self.num(322, '%s %d :%s' % (name, users, topic))
            self.num(323, ':End of /LIST')
        elif cmd == 'JOIN':
            ch = p[0] if p else ''
            key = p[1] if len(p) > 1 else ''
            low = ch.lower()
            if low == '#full':
                self.num(471, '%s :Cannot join channel (+l)' % ch)
            elif low == '#banned':
                self.num(474, '%s :Cannot join channel (+b)' % ch)
            elif low == '#keyed' and key != 'secret':
                self.num(475, '%s :Cannot join channel (+k)' % ch)
            elif low == '#silent':
                pass  # no answer: the client's 30 s limit
            elif low == '#nomode':
                self.send(':%s JOIN %s' % (self.me(), ch))   # join echo, but MODE is never answered
                self.channels['#nomode'] = None
            else:
                self.channels[low] = FAKE_TOPIC
                self.send(':%s JOIN :%s' % (self.me(), ch))
                self.num(332, '%s :%s' % (ch, FAKE_TOPIC))
                self.num(333, '%s opguy 1000000000' % ch)
                self.names(ch)
                if low == '#busy':
                    # channel events before the join completes (the mode reply): SPEC-AMEND-006 I2d.6
                    for e in [':bob!b@h.example PRIVMSG %s :said during the join' % ch,
                              ':carl!c@h.example JOIN %s' % ch,
                              ':opguy!o@h.example TOPIC %s :topic changed during the join' % ch]:
                        self.send(e)
        elif cmd == 'MODE':
            ch = p[0] if p else ''
            if len(p) == 1 and ch.lower() == '#modes':
                self.num(324, '%s +imnpstlk 10 key' % ch)
                self.num(329, '%s 1000000000' % ch)
            elif len(p) == 1 and ch.lower() != '#nomode':
                self.num(324, '%s +tn' % ch)
                self.num(329, '%s 1000000000' % ch)
            elif len(p) > 1 and ch.lower() == '#opchan':
                self.send(':%s MODE %s' % (self.me(), ' '.join(p)))
            elif len(p) > 1:
                self.num(482, '%s :You\'re not channel operator' % ch)
        elif cmd == 'NAMES':
            ch = p[0] if p else ''
            if ch.lower() == '#nosuch':
                self.num(403, '%s :No such channel' % ch)
            elif ch.lower() == '#slownames':
                pass
            else:
                self.names(ch)
        elif cmd == 'TOPIC':
            ch = p[0] if p else ''
            if len(p) > 1:
                self.channels[ch.lower()] = p[1]
                self.send(':%s TOPIC %s :%s' % (self.me(), ch, p[1]))
            elif ch.lower() == '#slowtopic':
                pass
            elif ch.lower() == '#notopic':
                self.num(331, '%s :No topic is set' % ch)
            elif ch.lower() == '#other':
                self.num(332, '%s :Topic B of other' % ch)
            else:
                self.num(332, '%s :%s' % (ch, self.channels.get(ch.lower()) or FAKE_TOPIC))
        elif cmd == 'WHOIS':
            n = p[0] if p else ''
            if n.lower() == 'nosuchnick':
                self.num(401, '%s :No such nick/channel' % n)
            elif n.lower() == 'fullwhois':
                self.num(311, '%s ~full full.host.example * :Real Name' % n)
                self.num(319, '%s :@#opchan' % n)
                self.num(312, '%s fake.server :Fake server' % n)
                self.num(317, '%s 5 1000000000 :seconds idle, signon time' % n)
            elif n.lower() == 'starhost':
                self.num(311, '%s %s * * :Real Name' % (n, n))
            elif n.lower() != 'silentwhois':
                self.num(311, '%s ~%s host-of-%s.example * :Real Name' % (n, n, n))
            self.num(318, '%s :End of /WHOIS list.' % n)
        elif cmd == 'PART':
            ch = p[0] if p else ''
            self.channels.pop(ch.lower(), None)
            self.send(':%s PART %s :Leaving' % (self.me(), ch))
        elif cmd == 'KICK':
            if p and p[0].lower() == '#opchan':
                self.send(':%s KICK %s %s :%s' % (self.me(), p[0], p[1] if len(p) > 1 else '',
                                                  p[2] if len(p) > 2 else ''))
            else:
                self.num(482, '%s :You\'re not channel operator' % (p[0] if p else ''))
        elif cmd in ('PRIVMSG', 'NOTICE'):
            if len(p) >= 2 and p[0].startswith('#') and p[1].strip() in (
                    'ev_msgs', 'ev_people', 'ev_kickme', 'ev_error', 'ev_close', 'ev_ping', 'ev_ctcp',
                    'ev_other'):
                self.event(p[0], p[1].strip())
        else:
            self.num(421, '%s :Unknown command' % cmd)


def fake(args):
    log = Log(args.log)
    ls = listener()
    ls.bind(('127.0.0.1', args.port))
    ls.listen(4)
    ls.settimeout(1.0)
    log('--', 'fake IRC server on 127.0.0.1:%d' % args.port)
    end = time.time() + args.timeout
    while time.time() < end:
        try:
            c, a = ls.accept()
        except socket.timeout:
            continue
        log('--', 'client connected from %s:%d' % a)
        fc = FakeClient(c, log, args)
        buf = b''
        c.settimeout(0.25)
        try:
            while time.time() < end:
                fc.tick()
                try:
                    d = c.recv(4096)
                except socket.timeout:
                    continue
                if not d:
                    log('--', 'client closed')
                    break
                buf += d
                lines, buf = split_lines(buf)
                for l in lines:
                    log('C>', show(l))
                    fc.line(l)
        except (ConnectionError, OSError) as e:
            log('--', 'connection ended: %s' % e)
        try:
            c.close()
        except OSError:
            pass
        if args.once:
            break
    ls.close()


# ---------------------------------------------------------------- peer

def peer(args):
    log = Log(args.log)
    host, port = args.server.rsplit(':', 1)
    s = socket.create_connection((host, int(port)), timeout=15)
    s.settimeout(0.5)
    ch = '#' + args.channel
    nick = args.nick
    state = {'buf': b'', 'target_in': False, 'done': False}

    def send(t):
        log('P>', show(t.encode('latin-1')))
        s.sendall(t.encode('latin-1') + b'\r\n')

    def pump(sec):
        end = time.time() + sec
        while time.time() < end:
            try:
                d = s.recv(4096)
            except socket.timeout:
                continue
            if not d:
                log('--', 'server closed')
                state['done'] = True
                return
            state['buf'] += d
            lines, state['buf'] = split_lines(state['buf'])
            for l in lines:
                t = l.decode('latin-1')
                if ' 322 ' in t:
                    continue
                log('S>', show(l))
                if t.startswith('PING'):
                    send('PONG' + t[4:])
                low = t.lower()
                tgt = ':' + args.target.lower() + '!'
                if low.startswith(tgt) and ' join ' in low:
                    state['target_in'] = True
                if low.startswith(tgt) and (' part ' in low or ' quit ' in low):
                    state['target_in'] = False
                    state['left'] = True

    send('NICK ' + nick)
    send('USER %s 127.0.0.1 %s :OpenMerc test peer' % (nick, host))
    pump(5)
    send('JOIN %s ' % ch)
    pump(3)
    log('--', 'waiting for %s in %s' % (args.target, ch))
    end = time.time() + args.wait
    while not state['target_in'] and time.time() < end and not state['done']:
        pump(1)
    if not state['target_in']:
        log('--', 'target did not join')
    else:
        pump(args.delay)
        t = args.target
        for line in ['PRIVMSG %s :hello from the peer' % ch,
                     'PRIVMSG %s :\x01ACTION waves from the peer\x01' % ch,
                     'NOTICE %s :a channel notice from the peer' % ch,
                     'PRIVMSG %s :a private hello' % t,
                     'PRIVMSG %s :\x01ACTION private action\x01' % t,
                     'NOTICE %s :a private notice' % t,
                     'PRIVMSG %s :\x01PING 4242\x01' % t]:
            send(line)
            pump(0.7)
        send('PART %s :brb' % ch)
        pump(2)
        send('JOIN %s ' % ch)
        pump(2)
        send('NICK %sx' % nick)
        nick += 'x'
        pump(2)
        # stay until the target leaves (its messages are logged)
        end = time.time() + args.linger
        while time.time() < end and not state.get('left') and not state['done']:
            pump(1)
    send('QUIT :peer done')
    pump(2)
    s.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='mode', required=True)
    p = sub.add_parser('proxy')
    p.add_argument('--port', type=int, default=16667)
    p.add_argument('--upstream', default='peerchat.openspy.net:6667')
    p.add_argument('--log')
    p.add_argument('--timeout', type=float, default=600)
    p.add_argument('--once', action='store_true')
    p.add_argument('--quiet-list', action='store_true', help='do not log 322 lines')
    f = sub.add_parser('fake')
    f.add_argument('--port', type=int, default=16668)
    f.add_argument('--log')
    f.add_argument('--timeout', type=float, default=600)
    f.add_argument('--once', action='store_true')
    q = sub.add_parser('peer')
    q.add_argument('--server', default='peerchat.openspy.net:6667')
    q.add_argument('--nick', required=True)
    q.add_argument('--channel', required=True)
    q.add_argument('--target', required=True)
    q.add_argument('--log')
    q.add_argument('--wait', type=float, default=240, help='seconds to wait for the target')
    q.add_argument('--delay', type=float, default=3, help='seconds after the target joined')
    q.add_argument('--linger', type=float, default=120, help='seconds to stay afterwards')
    args = ap.parse_args()
    {'proxy': proxy, 'fake': fake, 'peer': peer}[args.mode](args)


if __name__ == '__main__':
    main()
