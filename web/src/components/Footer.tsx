export function Footer() {
  return (
    <footer style={{ padding: '2rem', borderTop: '1px solid var(--surface-border)', marginTop: '4rem', textAlign: 'center', color: 'var(--text-muted)' }}>
      <p>© {new Date().getFullYear()} Chess. All rights reserved.</p>
    </footer>
  );
}
