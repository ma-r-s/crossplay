// GET /api/board-config
//
// The inbox page needs the board's address and its public key to sign in and
// read. Both are public by design (row security is what protects the data),
// but keeping them out of the HTML means rotating a key is a settings change
// on Vercel rather than a commit, and the same page works on every preview.

module.exports = function handler(req, res) {
  const url = process.env.SUPABASE_URL || "";
  const anonKey = process.env.SUPABASE_ANON_KEY || "";
  res.setHeader("Content-Type", "application/json");
  if (!url || !anonKey) {
    res.statusCode = 503;
    res.setHeader("Cache-Control", "no-store");
    res.end(
      JSON.stringify({ error: "The board is not set up on this deployment." }),
    );
    return;
  }
  res.statusCode = 200;
  res.setHeader("Cache-Control", "public, max-age=300");
  res.end(JSON.stringify({ url, anonKey }));
};
