#include <list>
#include <boost/foreach.hpp>
#include "mcts_tree.hpp"
#include "gtp_gogui.hpp"

extern Gtp::ReplWithGogui gtp;

MctsNode::MctsNode (Player player, Vertex v, double bias)
: player(player), v(v), has_all_legal_children (false), bias(bias)
{
  ASSERT2 (!isnan (bias), WW(bias));
  ASSERT2 (bias >= 0.0, WW(bias));
  ASSERT2 (bias <= 1.0, WW(bias));
  Reset ();
}

MctsNode::ChildrenList& MctsNode::Children () {
  return children;
}

Move MctsNode::GetMove () const {
  return Move(player, v);
}

void MctsNode::AddChild (const MctsNode& node) {
  children.push_front (node);
}

// TODO better implementation of child removation.
void MctsNode::RemoveChild (MctsNode* child_ptr) {
  ChildrenList::iterator child = children.begin();
  while (true) {
    ASSERT (child != children.end());
    if (&*child == child_ptr) {
      children.erase(child);
      return;
    }
    child++;
  }
}

bool MctsNode::ReadyToExpand () const {
  return stat.update_count() > 
    Param::prior_update_count + Param::mature_update_count;
}

MctsNode* MctsNode::FindChild (Move m) {
  // TODO make invariant about haveChildren and has_all_legal_children
  Player pl = m.GetPlayer();
  Vertex v  = m.GetVertex();
  ASSERT (has_all_legal_children [pl]);
  BOOST_FOREACH (MctsNode& child, children) {
    if (child.player == pl && child.v == v) {
      return &child;
    }
  }
  FAIL ("should not happen");
}

string MctsNode::ToString() const {
  stringstream s;
  s << player.ToGtpString() << " " 
    << v.ToGtpString() << " " 
    << stat.to_string() << " "
    << rave_stat.to_string() << " + "
    << bias << " -> "
    << Stat::Mix (stat,      Param::tree_stat_bias,
        rave_stat, Param::tree_rave_bias)
    // << " - ("  << stat.precision (Param::mcts_bias) << " : "
    // << stat.precision (Param::rave_bias) << ")"
    // << Stat::SlowMix (stat,
    //                   Param::mcts_bias,
    //                   rave_stat,
    //                   Param::rave_bias)
    ;

  return s.str();
}

namespace {
  bool SubjectiveCmp (const MctsNode* a, const MctsNode* b) {
    return a->stat.update_count() > b->stat.update_count();
    // return SubjectiveMean () > b->SubjectiveMean ();
  }
}

void MctsNode::RecPrint (ostream& out, uint depth, float min_visit, uint max_children) const {
  rep (d, depth) out << "  ";
  out << ToString () << endl;

  vector <const MctsNode*> child_tab;
  BOOST_FOREACH (const MctsNode& child, children) {
    child_tab.push_back(&child);
  }

  sort (child_tab.begin(), child_tab.end(), SubjectiveCmp);
  if (child_tab.size () > max_children) child_tab.resize(max_children);

  BOOST_FOREACH (const MctsNode* child, child_tab) {
    if (child->stat.update_count() >= min_visit) {
      child->RecPrint (out, depth + 1, min_visit, max(1u, max_children - 1));
    }
  }
}

string MctsNode::RecToString (float min_visit, uint max_children) const { 
  ostringstream out;
  RecPrint (out, 0, min_visit, max_children); 
  return out.str ();
}

const MctsNode& MctsNode::MostExploredChild (Player pl) const {
  const MctsNode* best = NULL;
  float best_update_count = -1;

  ASSERT (has_all_legal_children [pl]);

  BOOST_FOREACH (const MctsNode& child, children) {
    if (child.player == pl && child.stat.update_count() > best_update_count) {
      best_update_count = child.stat.update_count();
      best = &child;
    }
  }

  ASSERT (best != NULL);
  return *best;
}


MctsNode& MctsNode::BestRaveChild (Player pl) {
  MctsNode* best_child = NULL;
  float best_urgency = -100000000000000.0; // TODO infinity
  const float log_val = log (stat.update_count());

  ASSERT (has_all_legal_children [pl]);

  BOOST_FOREACH (MctsNode& child, Children()) {
    if (child.player != pl) continue;
    float child_urgency = child.SubjectiveRaveValue (pl, log_val);
    if (child_urgency > best_urgency) {
      best_urgency = child_urgency;
      best_child   = &child;
    }
  }

  ASSERT (best_child != NULL); // at least pass
  return *best_child;
}


void MctsNode::Reset () {
  has_all_legal_children.SetAll (false);
  children.clear ();
  stat.reset      (Param::prior_update_count,
      player.SubjectiveScore (Param::prior_mean));
  rave_stat.reset (Param::prior_update_count,
      player.SubjectiveScore (Param::prior_mean));
}

float MctsNode::SubjectiveMean () const {
  return player.SubjectiveScore (stat.mean ());
}

float MctsNode::SubjectiveRaveValue (Player pl, float log_val) const {
  float value;

  if (Param::tree_rave_use) {
    value = Stat::Mix (stat,      Param::tree_stat_bias,
        rave_stat, Param::tree_rave_bias);
  } else {
    value = stat.mean ();
  }

  return
    pl.SubjectiveScore (value)
    + Param::tree_explore_coeff * sqrt (log_val / stat.update_count())
    + Param::tree_progressive_bias * bias / stat.update_count ();
  // TODO other equation for PB
}


// -----------------------------------------------------------------------------

Mcts::Mcts () :
  root (Player::White(), Vertex::Any (), 0.0)
{
  act_root = &root;
  gtp.RegisterGfx ("MCTS.show",    "0 4", this, &Mcts::GtpShowTree);
  gtp.RegisterGfx ("MCTS.show",   "10 4", this, &Mcts::GtpShowTree);
  gtp.RegisterGfx ("MCTS.show",  "100 4", this, &Mcts::GtpShowTree);
  gtp.RegisterGfx ("MCTS.show", "1000 4", this, &Mcts::GtpShowTree);
}

void Mcts::Reset () {
  root.Reset ();
}

void Mcts::SyncRoot (const Board& board, const Gammas& gammas) {
  // TODO replace this by FatBoard
  Board sync_board;
  Sampler sampler(sync_board, gammas);
  sampler.NewPlayout ();

  act_root = &root;
  BOOST_FOREACH (Move m, board.Moves ()) {
    EnsureAllLegalChildren (act_root, m.GetPlayer(), sync_board, sampler);
    act_root = act_root->FindChild (m);
    CHECK (sync_board.IsLegal (m));
    sync_board.PlayLegal (m);
    sampler.MovePlayed();
  }

  Player pl = board.ActPlayer();
  EnsureAllLegalChildren (act_root, pl, board, sampler);
  RemoveIllegalChildren (act_root, pl, board);
}


Move Mcts::BestMove (Player player) {
  const MctsNode& best_node = act_root->MostExploredChild (player);

  return
    best_node.SubjectiveMean() < Param::resign_mean ?
    Move::Invalid() :
    Move (player, best_node.v);
}


void Mcts::NewPlayout (){
  trace.clear();
  trace.push_back (act_root);
  move_history.clear ();
  move_history.push_back (act_root->GetMove());
  tree_phase = Param::tree_use;
  tree_move_count = 0;
}

void Mcts::EnsureAllLegalChildren (MctsNode* node, Player pl, const Board& board, const Sampler& sampler) {
  if (node->has_all_legal_children [pl]) return;
  empty_v_for_each_and_pass (&board, v, {
      // superko nodes have to be removed from the tree later
      if (board.IsLegal (pl, v)) {
      double bias = sampler.Probability (pl, v);
      node->AddChild (MctsNode(pl, v, bias));
      }
      });
  node->has_all_legal_children [pl] = true;
}


void Mcts::RemoveIllegalChildren (MctsNode* node, Player pl, const Board& full_board) {
  ASSERT (node->has_all_legal_children [pl]);

  MctsNode::ChildrenList::iterator child = node->children.begin();
  while (child != node->children.end()) {
    if (child->player == pl && !full_board.IsReallyLegal (Move (pl, child->v))) {
      node->children.erase (child++);
    } else {
      ++child;
    }
  }
}


void Mcts::NewMove (Move m) {
  move_history.push_back (m);
}

Move Mcts::ChooseMove (Board& play_board, const Sampler& sampler) {
  Player pl = play_board.ActPlayer();

  if (!tree_phase || tree_move_count >= Param::tree_max_moves) {
    return Move::Invalid();
  }

  if (!ActNode().has_all_legal_children [pl]) {
    if (!ActNode().ReadyToExpand ()) {
      tree_phase = false;
      return Move::Invalid();
    }
    ASSERT (pl == ActNode().player.Other());
    EnsureAllLegalChildren (&ActNode(), pl, play_board, sampler);
  }

  MctsNode& uct_child = ActNode().BestRaveChild (pl);
  trace.push_back (&uct_child);
  ASSERT (uct_child.v != Vertex::Any());
  tree_move_count += 1;
  return Move (pl, uct_child.v);
}

void Mcts::UpdateTraceRegular (float score) {
  BOOST_FOREACH (MctsNode* node, trace) {
    node->stat.update (score);
  }

  if (Param::tree_rave_update) {
    UpdateTraceRave (score);
  }
}

void Mcts::UpdateTraceRave (float score) {
  // TODO configure rave blocking through options

  uint last_ii  = move_history.size () * Param::tree_rave_update_fraction;
  // TODO tune that

  rep (act_ii, trace.size()) {
    // Mark moves that should be updated in RAVE children of: trace [act_ii]
    NatMap <Move, bool> do_update (false);
    NatMap <Move, bool> do_update_set_to (true);
    ForEachNat (Player, pl) do_update_set_to [Move (pl, Vertex::Pass())] = false;

    // TODO this is the slow and too-fixed part
    // TODO Change it to weighting with flexible masking.
    reps (jj, act_ii+1, last_ii) {
      Move m = move_history [jj];
      do_update [m] = do_update_set_to [m];
      do_update_set_to [m] = false;
      do_update_set_to [m.OtherPlayer()] = false;
    }

    // Do the update.
    BOOST_FOREACH (MctsNode& child, trace[act_ii]->Children()) {
      if (do_update [child.GetMove()]) {
        child.rave_stat.update (score);
      }
    }
  }
}

MctsNode& Mcts::ActNode() {
  ASSERT (trace.size() > 0);
  return *trace.back ();
}


void Mcts::GtpShowTree (Gtp::Io& io) {
  uint min_updates  = io.Read <uint> ();
  uint max_children = io.Read <uint> ();
  io.CheckEmpty();
  io.out << endl << act_root->RecToString (min_updates, max_children);
}

