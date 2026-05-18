const API_BASE = 'http://localhost:8765';

export const api = {
  async fetch(endpoint: string, options: RequestInit = {}) {
    const token = localStorage.getItem('chess_token');
    
    const headers: Record<string, string> = {
      'Content-Type': 'application/json',
      ...((options.headers as Record<string, string>) || {}),
    };

    if (token) {
      headers['Authorization'] = `Bearer ${token}`;
    }

    const response = await fetch(`${API_BASE}${endpoint}`, {
      ...options,
      headers,
    });

    if (!response.ok) {
      const errorData = await response.json().catch(() => ({}));
      throw new Error(errorData.detail || 'API request failed');
    }

    return response.json();
  },

  async register(username: string, password: string) {
    return this.fetch('/auth/register', {
      method: 'POST',
      body: JSON.stringify({ username, password }),
    });
  },

  async login(username: string, password: string) {
    const data = await this.fetch('/auth/login', {
      method: 'POST',
      body: JSON.stringify({ username, password }),
    });
    if (data.token) {
      localStorage.setItem('chess_token', data.token);
    }
    return data;
  },

  logout() {
    localStorage.removeItem('chess_token');
  },

  async getProfile() {
    return this.fetch('/me');
  },

  async getLeaderboard() {
    return this.fetch('/leaderboard');
  },

  async createCheckoutSession() {
    return this.fetch('/stripe/create-checkout-session', { method: 'POST' });
  },

  async cancelSubscription() {
    return this.fetch('/stripe/cancel-subscription', { method: 'POST' });
  },
};
