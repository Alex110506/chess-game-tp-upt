import { Link, useNavigate } from 'react-router-dom';
import { Crown, LogOut, User } from 'lucide-react';
import { api } from '../lib/api';

export function Navbar() {
  const navigate = useNavigate();
  const token = localStorage.getItem('chess_token');

  const handleLogout = () => {
    api.logout();
    navigate('/login');
  };

  return (
    <nav style={{ padding: '1rem 2rem', borderBottom: '1px solid var(--surface-border)', backgroundColor: 'rgba(18, 18, 18, 0.8)', backdropFilter: 'blur(10px)', position: 'sticky', top: 0, zIndex: 100 }}>
      <div className="container" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
        <Link to="/" style={{ display: 'flex', alignItems: 'center', gap: '0.5rem', fontSize: '1.5rem', fontWeight: 700 }}>
          <Crown className="text-gold" />
          <span className="text-gradient">UPT Chess</span>
        </Link>
        <div style={{ display: 'flex', gap: '1rem' }}>
          {token ? (
            <>
              <Link to="/account" className="btn btn-outline">
                <User size={18} /> Account
              </Link>
              <button onClick={handleLogout} className="btn btn-outline" style={{ color: 'var(--danger)' }}>
                <LogOut size={18} /> Logout
              </button>
            </>
          ) : (
            <>
              <Link to="/login" className="btn btn-outline">Login</Link>
              <Link to="/register" className="btn btn-primary">Play Ranked</Link>
            </>
          )}
        </div>
      </div>
    </nav>
  );
}
