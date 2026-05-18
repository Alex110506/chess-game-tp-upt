import { useEffect, useState } from 'react';
import { Link } from 'react-router-dom';
import { Bot, Globe, Puzzle, Trophy, Zap, Download } from 'lucide-react';
import { api } from '../lib/api';

export function Home() {
  const [leaderboard, setLeaderboard] = useState<any[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    api.getLeaderboard()
      .then(data => {
        setLeaderboard(data.top || []);
      })
      .catch(console.error)
      .finally(() => setLoading(false));
  }, []);

  return (
    <div className="animate-fade-in">
      {/* Hero Section */}
      <section style={{ padding: '6rem 0', textAlign: 'center' }}>
        <div className="container">
          <div style={{ display: 'inline-block', padding: '0.25rem 1rem', background: 'rgba(46, 105, 56, 0.2)', border: '1px solid var(--primary)', borderRadius: '999px', marginBottom: '2rem', color: 'var(--primary-light)' }}>
            <Zap size={14} style={{ display: 'inline', marginRight: '0.5rem', verticalAlign: 'text-top' }} />
            Coming Soon: AI Chess Coach
          </div>
          <h1 style={{ fontSize: '4rem', marginBottom: '1.5rem', maxWidth: '800px', margin: '0 auto 1.5rem' }}>
            Master the Board with <span className="text-gradient">Precision</span>
          </h1>
          <p style={{ fontSize: '1.25rem', color: 'var(--text-muted)', maxWidth: '600px', margin: '0 auto 2.5rem' }}>
            Play online with ELO ranking, solve handcrafted puzzles, or prepare to learn from your personalized AI Coach.
          </p>
          <div style={{ display: 'flex', gap: '1rem', justifyContent: 'center' }}>
            <Link to="/register" className="btn btn-primary" style={{ padding: '1rem 2rem', fontSize: '1.125rem' }}>
              <Globe size={20} /> Play Online
            </Link>
            <a href="#features" className="btn btn-outline" style={{ padding: '1rem 2rem', fontSize: '1.125rem' }}>
              Explore Features
            </a>
          </div>
        </div>
      </section>

      {/* AI Coach Banner */}
      <section className="container mt-8 animate-fade-in delay-100">
        <div className="glass-panel" style={{ padding: '4rem', display: 'flex', alignItems: 'center', justifyContent: 'space-between', background: 'linear-gradient(135deg, rgba(36,36,36,0.8), rgba(46,105,56,0.2))' }}>
          <div style={{ maxWidth: '500px' }}>
            <h2 className="text-gold" style={{ display: 'flex', alignItems: 'center', gap: '1rem' }}>
              <Bot size={36} /> AI Coach
            </h2>
            <p style={{ fontSize: '1.125rem', color: 'var(--text-muted)', marginTop: '1rem' }}>
              Elevate your game with real-time analysis, personalized training plans, and deep insights from our state-of-the-art AI. Coming in the next update.
            </p>
            <Link to="/account" className="btn btn-gold mt-4">
              View Billing Options
            </Link>
          </div>
          <div style={{ padding: '2rem', background: 'rgba(0,0,0,0.3)', borderRadius: '16px', border: '1px solid var(--surface-border)' }}>
             <pre style={{ color: 'var(--primary-light)', fontFamily: 'monospace', fontSize: '0.875rem' }}>
               {`[Analysis] Inaccuracy
Your move: Nd4
Best move: Bc5

Reasoning:
Bc5 develops a piece while 
controlling the center and 
preparing to castle.`}
             </pre>
          </div>
        </div>
      </section>

      {/* Features Grid */}
      <section id="features" className="container" style={{ padding: '6rem 0' }}>
        <h2 className="text-center mb-8 text-gradient">Everything you need to improve</h2>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(300px, 1fr))', gap: '2rem' }}>
          
          <div className="glass-panel" style={{ padding: '2rem' }}>
            <div style={{ width: '48px', height: '48px', background: 'rgba(255,255,255,0.1)', borderRadius: '12px', display: 'flex', alignItems: 'center', justifyContent: 'center', marginBottom: '1.5rem', color: 'var(--primary-light)' }}>
              <Globe size={24} />
            </div>
            <h3>Ranked Multiplayer</h3>
            <p className="mt-4" style={{ color: 'var(--text-muted)' }}>
              Host or join rooms instantly. Compete globally and climb the ELO ladder.
            </p>
          </div>

          <div className="glass-panel" style={{ padding: '2rem' }}>
            <div style={{ width: '48px', height: '48px', background: 'rgba(255,255,255,0.1)', borderRadius: '12px', display: 'flex', alignItems: 'center', justifyContent: 'center', marginBottom: '1.5rem', color: 'var(--accent-gold)' }}>
              <Puzzle size={24} />
            </div>
            <h3>Tactical Puzzles</h3>
            <p className="mt-4" style={{ color: 'var(--text-muted)' }}>
              Sharpen your vision with hand-crafted mate-in-N puzzles defended by Stockfish.
            </p>
          </div>

          <div className="glass-panel" style={{ padding: '2rem' }}>
            <div style={{ width: '48px', height: '48px', background: 'rgba(255,255,255,0.1)', borderRadius: '12px', display: 'flex', alignItems: 'center', justifyContent: 'center', marginBottom: '1.5rem', color: '#fff' }}>
              <Download size={24} />
            </div>
            <h3>Native C Client</h3>
            <p className="mt-4" style={{ color: 'var(--text-muted)' }}>
              Play through a blazing-fast Raylib GUI or directly in your terminal.
            </p>
          </div>

        </div>
      </section>

      {/* Leaderboard Section */}
      <section className="container mb-8">
        <div className="glass-panel" style={{ padding: '3rem' }}>
          <h2 style={{ display: 'flex', alignItems: 'center', gap: '1rem', marginBottom: '2rem' }}>
            <Trophy className="text-gold" size={32} /> Global Leaderboard
          </h2>
          
          {loading ? (
            <p style={{ color: 'var(--text-muted)' }}>Loading top players...</p>
          ) : (
            <div style={{ overflowX: 'auto' }}>
              <table style={{ width: '100%', textAlign: 'left', borderCollapse: 'collapse' }}>
                <thead>
                  <tr style={{ borderBottom: '1px solid var(--surface-border)' }}>
                    <th style={{ padding: '1rem', color: 'var(--text-muted)' }}>Rank</th>
                    <th style={{ padding: '1rem', color: 'var(--text-muted)' }}>Player</th>
                    <th style={{ padding: '1rem', color: 'var(--text-muted)' }}>ELO</th>
                    <th style={{ padding: '1rem', color: 'var(--text-muted)' }}>W / L / T</th>
                  </tr>
                </thead>
                <tbody>
                  {leaderboard.map((user, idx) => (
                    <tr key={user.username} style={{ borderBottom: '1px solid rgba(255,255,255,0.05)', transition: 'background 0.2s' }} onMouseEnter={e => e.currentTarget.style.background = 'rgba(255,255,255,0.02)'} onMouseLeave={e => e.currentTarget.style.background = 'transparent'}>
                      <td style={{ padding: '1rem', fontWeight: 'bold', color: idx < 3 ? 'var(--accent-gold)' : 'inherit' }}>#{idx + 1}</td>
                      <td style={{ padding: '1rem' }}>{user.username}</td>
                      <td style={{ padding: '1rem', color: 'var(--primary-light)', fontWeight: 'bold' }}>{user.rank}</td>
                      <td style={{ padding: '1rem', color: 'var(--text-muted)' }}>{user.wins} / {user.losses} / {user.ties}</td>
                    </tr>
                  ))}
                  {leaderboard.length === 0 && (
                    <tr>
                      <td colSpan={4} style={{ padding: '1rem', textAlign: 'center', color: 'var(--text-muted)' }}>No players ranked yet.</td>
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
