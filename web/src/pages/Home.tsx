import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { Bot, Clock, Globe, Lightbulb, Puzzle, Shield, Sword, Terminal, Trophy, Zap } from 'lucide-react';
import { api } from '../lib/api';

export function Home() {
  const [leaderboard, setLeaderboard] = useState<any[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    api.getLeaderboard()
      .then(data => setLeaderboard(data.top || []))
      .catch(console.error)
      .finally(() => setLoading(false));
  }, []);

  return (
    <div className="animate-fade-in">

      {/* Hero */}
      <section style={{ padding: '7rem 0 5rem', textAlign: 'center' }}>
        <div className="container">
          <div style={{ display: 'inline-flex', alignItems: 'center', gap: '0.5rem', padding: '0.35rem 1.1rem', background: 'rgba(46,105,56,0.18)', border: '1px solid var(--primary)', borderRadius: '999px', marginBottom: '2rem', color: 'var(--primary-light)', fontSize: '0.9rem' }}>
            <Zap size={14} />
            AI Coach Pro — now available
          </div>
          <h1 style={{ fontSize: 'clamp(2.5rem, 6vw, 4.5rem)', lineHeight: 1.1, marginBottom: '1.5rem', maxWidth: '820px', margin: '0 auto 1.5rem' }}>
            Play, Compete &amp; Improve with <span className="text-gradient">Precision</span>
          </h1>
          <p style={{ fontSize: '1.2rem', color: 'var(--text-muted)', maxWidth: '580px', margin: '0 auto 2.5rem', lineHeight: 1.7 }}>
            Ranked online chess with ELO, Stockfish-powered bot and hints, handcrafted puzzles, chess clock, and a personal AI coach — all in one place.
          </p>
          <div style={{ display: 'flex', gap: '1rem', justifyContent: 'center', flexWrap: 'wrap' }}>
            <Link to="/register" className="btn btn-primary" style={{ padding: '0.9rem 2rem', fontSize: '1.1rem' }}>
              <Globe size={20} /> Play Ranked
            </Link>
            <a href="#features" className="btn btn-outline" style={{ padding: '0.9rem 2rem', fontSize: '1.1rem' }}>
              Explore Features
            </a>
          </div>
        </div>
      </section>

      {/* AI Coach Banner */}
      <section className="container animate-fade-in delay-100">
        <div className="glass-panel" style={{ padding: '3rem 4rem', display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: '3rem', flexWrap: 'wrap', background: 'linear-gradient(135deg, rgba(36,36,36,0.85), rgba(46,105,56,0.18))' }}>
          <div style={{ maxWidth: '480px' }}>
            <h2 className="text-gold" style={{ display: 'flex', alignItems: 'center', gap: '0.75rem', marginBottom: '1rem' }}>
              <Bot size={32} /> AI Coach Pro
            </h2>
            <p style={{ fontSize: '1.1rem', color: 'var(--text-muted)', lineHeight: 1.7, marginBottom: '1.5rem' }}>
              Subscribe for $4.99/month and get post-game move analysis, tactical missed-opportunity alerts, and a personalized opening coach — all powered by our state-of-the-art AI.
            </p>
            <div style={{ display: 'flex', gap: '1rem', flexWrap: 'wrap' }}>
              <Link to="/account" className="btn btn-gold">
                Get AI Coach Pro
              </Link>
              <Link to="/login" className="btn btn-outline">
                Sign In
              </Link>
            </div>
          </div>
          <div style={{ flexShrink: 0, padding: '1.75rem 2rem', background: 'rgba(0,0,0,0.35)', borderRadius: '14px', border: '1px solid var(--surface-border)', fontFamily: 'monospace', fontSize: '0.85rem' }}>
            <div style={{ color: 'var(--text-muted)', marginBottom: '0.75rem', fontSize: '0.75rem', textTransform: 'uppercase', letterSpacing: '0.1em' }}>Post-game analysis</div>
            <div style={{ color: '#f87171' }}>⚠  Inaccuracy on move 14</div>
            <div style={{ marginTop: '0.5rem', color: 'var(--text-muted)' }}>Your move: <span style={{ color: 'white' }}>Nd4</span></div>
            <div style={{ color: 'var(--text-muted)' }}>Best move: <span style={{ color: 'var(--primary-light)' }}>Bc5</span></div>
            <div style={{ marginTop: '0.75rem', color: 'var(--text-muted)', maxWidth: '240px', lineHeight: 1.5 }}>
              Bc5 develops a piece, controls the center and prepares queenside castling.
            </div>
          </div>
        </div>
      </section>

      {/* Features Grid */}
      <section id="features" className="container" style={{ padding: '6rem 0 4rem' }}>
        <h2 className="text-center mb-8 text-gradient">Everything you need to improve</h2>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(280px, 1fr))', gap: '1.5rem' }}>

          <FeatureCard icon={<Globe size={22} />} iconColor="var(--primary-light)" title="Ranked Multiplayer">
            Host or join rooms with a 4-character code. Every ranked game updates both players' ELO via the leaderboard server.
          </FeatureCard>

          <FeatureCard icon={<Puzzle size={22} />} iconColor="var(--accent-gold)" title="Tactical Puzzles">
            9 handcrafted mate-in-N puzzles across Easy, Medium, and Hard tiers. Stockfish defends at depth 14 for maximum resistance.
          </FeatureCard>

          <FeatureCard icon={<Sword size={22} />} iconColor="#a78bfa" title="Play vs Bot">
            Three difficulty levels: Easy (depth 1), Medium (depth 5), Hard (depth 12) — powered by the Stockfish engine.
          </FeatureCard>

          <FeatureCard icon={<Lightbulb size={22} />} iconColor="#fbbf24" title="Hint System">
            Stuck? Request a Stockfish-powered move suggestion at any point during local or bot games.
          </FeatureCard>

          <FeatureCard icon={<Clock size={22} />} iconColor="#34d399" title="Chess Clock">
            Optional 1, 5, or 10-minute countdown for Local 1v1 and Online games. Automatic flag detection when time runs out.
          </FeatureCard>

          <FeatureCard icon={<Shield size={22} />} iconColor="#60a5fa" title="ELO Ranking">
            Accounts start at 1200 ELO. Wins and losses update both players' ratings. Climb the global leaderboard.
          </FeatureCard>

          <FeatureCard icon={<Bot size={22} />} iconColor="var(--accent-gold)" title="AI Coach Pro">
            $4.99/month subscription for post-game analysis, tactical alerts, and a personalized opening coach.
          </FeatureCard>

          <FeatureCard icon={<Terminal size={22} />} iconColor="#94a3b8" title="Native C Client">
            A Raylib GUI and a terminal client — both share the same game engine. No browser required.
          </FeatureCard>

        </div>
      </section>

      {/* Leaderboard */}
      <section className="container mb-8">
        <div className="glass-panel" style={{ padding: '3rem' }}>
          <h2 style={{ display: 'flex', alignItems: 'center', gap: '0.75rem', marginBottom: '2rem' }}>
            <Trophy className="text-gold" size={28} /> Global Leaderboard
          </h2>
          {loading ? (
            <p style={{ color: 'var(--text-muted)' }}>Loading top players…</p>
          ) : (
            <div style={{ overflowX: 'auto' }}>
              <table style={{ width: '100%', textAlign: 'left', borderCollapse: 'collapse' }}>
                <thead>
                  <tr style={{ borderBottom: '1px solid var(--surface-border)' }}>
                    <th style={{ padding: '0.75rem 1rem', color: 'var(--text-muted)', fontWeight: 500 }}>#</th>
                    <th style={{ padding: '0.75rem 1rem', color: 'var(--text-muted)', fontWeight: 500 }}>Player</th>
                    <th style={{ padding: '0.75rem 1rem', color: 'var(--text-muted)', fontWeight: 500 }}>ELO</th>
                    <th style={{ padding: '0.75rem 1rem', color: 'var(--text-muted)', fontWeight: 500 }}>W / L / T</th>
                  </tr>
                </thead>
                <tbody>
                  {leaderboard.map((user, idx) => (
                    <tr
                      key={user.username}
                      style={{ borderBottom: '1px solid rgba(255,255,255,0.04)', transition: 'background 0.15s' }}
                      onMouseEnter={e => (e.currentTarget.style.background = 'rgba(255,255,255,0.03)')}
                      onMouseLeave={e => (e.currentTarget.style.background = 'transparent')}
                    >
                      <td style={{ padding: '0.9rem 1rem', fontWeight: 700, color: idx === 0 ? '#ffd700' : idx === 1 ? '#c0c0c0' : idx === 2 ? '#cd7f32' : 'var(--text-muted)' }}>
                        #{idx + 1}
                      </td>
                      <td style={{ padding: '0.9rem 1rem', fontWeight: 500 }}>{user.username}</td>
                      <td style={{ padding: '0.9rem 1rem', color: 'var(--primary-light)', fontWeight: 700 }}>{user.rank}</td>
                      <td style={{ padding: '0.9rem 1rem', color: 'var(--text-muted)' }}>{user.wins} / {user.losses} / {user.ties}</td>
                    </tr>
                  ))}
                  {leaderboard.length === 0 && (
                    <tr>
                      <td colSpan={4} style={{ padding: '1.5rem', textAlign: 'center', color: 'var(--text-muted)' }}>
                        No players ranked yet. <Link to="/register" style={{ color: 'var(--primary-light)' }}>Be the first.</Link>
                      </td>
                    </tr>
                  )}
                </tbody>
              </table>
            </div>
          )}
        </div>
      </section>

    </div>
  );
}

function FeatureCard({ icon, iconColor, title, children }: {
  icon: React.ReactNode;
  iconColor: string;
  title: string;
  children: React.ReactNode;
}) {
  return (
    <div className="glass-panel" style={{ padding: '2rem' }}>
      <div style={{ width: '44px', height: '44px', background: 'rgba(255,255,255,0.07)', borderRadius: '10px', display: 'flex', alignItems: 'center', justifyContent: 'center', marginBottom: '1.25rem', color: iconColor }}>
        {icon}
      </div>
      <h3 style={{ marginBottom: '0.75rem' }}>{title}</h3>
      <p style={{ color: 'var(--text-muted)', lineHeight: 1.65, fontSize: '0.95rem' }}>{children}</p>
    </div>
  );
}
