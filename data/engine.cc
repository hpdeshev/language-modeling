#include "src/engine.h"

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>

#include "src/board.h"
#include "src/command.h"
#include "src/common.h"
#include "src/config.h"
#include "src/coord.h"
#include "src/history_item.h"
#include "src/move_command.h"
#include "src/take_command.h"
#include "src/options.h"

namespace checkers_style_game {

class Client final : public Engine::Observer, public Engine::Logger {
 public:
  explicit Client(Side side_to_move) : side_to_move_{side_to_move} {}

  void Reset() {
    side_that_wins_ = Side::kUnset;
  }

  Side side_to_move() const { return side_to_move_; }
  Side side_that_wins() const { return side_that_wins_; }

 private:
  void Log(Level, const std::string&) override {}
  void OnGameStarted(int8_t) override {}
  void OnGameUpdated(Side, const Board::Data&) override {}
  void OnGameEnded(Side side_that_wins) override {
    side_that_wins_ = side_that_wins;
  }

  Side side_to_move_{Side::kUnset};
  Side side_that_wins_{Side::kUnset};
};

Engine::Ptr Engine::Create(const Engine::Observer& observer,
                           const Logger& logger) {
  return Ptr{new Engine{observer, logger}};
}

bool Engine::StartGame(const Options* options) {
  logger_.Log(Logger::Level::kInfo, __func__ + std::string{'\n'});
  side_that_wins_ = Side::kUnset;
  options_ = (options ? *options : Options{});
  if (!options_.data.empty()) {
    side_to_move_ = options_.side_to_move;
    board_.Reset(options_.data);
    num_seq_moves_ = options_.num_seq_moves;
  } else {
    side_to_move_ = Side::kLight;
    board_.Reset();
    num_seq_moves_ = 0;
  }
  history_.clear();
  observer_.OnGameStarted(Config::kBoardSize);

  if (GetCount(side_to_move_).empty()) {
    observer_.OnGameUpdated(Side::kUnset, static_cast<Board::Data>(board_));
    if (GetCount(Reverse(side_to_move_)).empty()) {
      OnGameEnded(Side::kNeutral);
    } else {
      OnGameEnded(Reverse(side_to_move_));
    }
    side_to_move_ = Side::kUnset;
    return true;
  }

  if (GetCount(Reverse(side_to_move_)).empty()) {
    observer_.OnGameUpdated(Side::kUnset, static_cast<Board::Data>(board_));
    OnGameEnded(side_to_move_);
    side_to_move_ = Side::kUnset;
    return true;
  }

  auto can_move = this->CanMove(side_to_move_);
  auto can_take = this->CanTake(side_to_move_);
  if (!can_move && !can_take) {
    observer_.OnGameUpdated(Side::kUnset, static_cast<Board::Data>(board_));
    OnGameEnded(Reverse(side_to_move_));
    side_to_move_ = Side::kUnset;
    return true;
  }

  SetupComputers();

  observer_.OnGameUpdated(side_to_move_, static_cast<Board::Data>(board_));

  std::list<Command::Ptr> commands;
  if (can_take) {
    commands = GetTakes(side_to_move_);
  } else {
    if (GameType::kAnalysis != options_.game_type) {
      commands = GetMoves(side_to_move_);
    }
  }
  if (commands.size() == 1) {
    auto& cmd = commands.front();
    auto p_cmd = cmd.get();
    if (options_.has_history) {
      history_.emplace_back(std::move(cmd));
    }
    p_cmd->Execute();
  }

  auto comp = GetComputerToMove();
  if (comp) {
    comp->Proceed();
  }

  return true;
}

bool Engine::TryAt(int8_t x, int8_t y) {
  std::ostringstream oss;
  oss << __func__ << " (x=" << +x << ",y=" << +y << ")";
  logger_.Log(Logger::Level::kInfo, oss.str() + std::string{'\n'});

  std::bitset<static_cast<size_t>(AllDirections())> dirs{};

  auto result = true;
  if (CanTake(x, y, MoveDirection::kTopLeft)) {
    dirs |= static_cast<int8_t>(MoveDirection::kTopLeft);
  }
  if (CanTake(x, y, MoveDirection::kTopRight)) {
    dirs |= static_cast<int8_t>(MoveDirection::kTopRight);
  }
  if (CanTake(x, y, MoveDirection::kBottomLeft)) {
    dirs |= static_cast<int8_t>(MoveDirection::kBottomLeft);
  }
  if (CanTake(x, y, MoveDirection::kBottomRight)) {
    dirs |= static_cast<int8_t>(MoveDirection::kBottomRight);
  }
  if (dirs.count() == 1) {
    result = Take(x, y, static_cast<MoveDirection>(dirs.to_ullong()));
  } else if (dirs.count() == 0) {
    if (CanMove(x, y, MoveDirection::kTopLeft)) {
      dirs |= static_cast<int8_t>(MoveDirection::kTopLeft);
    }
    if (CanMove(x, y, MoveDirection::kTopRight)) {
      dirs |= static_cast<int8_t>(MoveDirection::kTopRight);
    }
    if (CanMove(x, y, MoveDirection::kBottomLeft)) {
      dirs |= static_cast<int8_t>(MoveDirection::kBottomLeft);
    }
    if (CanMove(x, y, MoveDirection::kBottomRight)) {
      dirs |= static_cast<int8_t>(MoveDirection::kBottomRight);
    }
    if (dirs.count() == 1) {
      result = Move(x, y, static_cast<MoveDirection>(dirs.to_ullong()));
    } else {
      result = false;
    }
  } else {
    result = false;
  }
  return result;
}

bool Engine::Revert() {
  if (!options_.has_history) {
    return false;
  }

  if (history_.empty()) {
    return false;
  }

  auto& cmd = history_.back();
  cmd->Revert();
  history_.pop_back();

  return true;
}

bool Engine::CanMove(Side side) const {
  for (const auto& row : board_) {
    for (auto piece_it = row.begin(); piece_it != row.end(); ++piece_it) {
      auto& piece = *piece_it;
      if (piece && (piece->side() == side)) {
        Coord coord = piece_it.GetCoord();
        if (CanMove(coord.x(), coord.y())) {
          return true;
        }
      }
    }
  }
  return false;
}

bool Engine::CanMove(int8_t x, int8_t y) const {
  return (CanMove(x, y, MoveDirection::kTopLeft) ||
          CanMove(x, y, MoveDirection::kTopRight) ||
          CanMove(x, y, MoveDirection::kBottomLeft) ||
          CanMove(x, y, MoveDirection::kBottomRight));
}

bool Engine::CanMove(int8_t x, int8_t y, MoveDirection dir) const {
  if (MoveDirection::kUnset == dir) {
    return false;
  }

  try {
    Coord coord{x, y};
    const Piece* const& pbeg = board_(coord);
    if (!pbeg) {
      return false;
    }

    if (pbeg->side() != side_to_move_) {
      return false;
    }

    if (pbeg->level() == Level::kMan) {
      if (pbeg->side() == Side::kLight) {
        if ((dir == MoveDirection::kBottomLeft) ||
            (dir == MoveDirection::kBottomRight)) {
          return false;
        }
      } else /*Side::kDark*/ {
        if ((dir == MoveDirection::kTopLeft) ||
            (dir == MoveDirection::kTopRight)) {
          return false;
        }
      }
    }

    const Piece* const& pend = board_(static_cast<int8_t>(coord.x() + Dx(dir)),
                                      static_cast<int8_t>(coord.y() + Dy(dir)));
    if (pend) {
      return false;
    }
  } catch (std::exception&) {
    return false;
  }
  return true;
}

bool Engine::Move(int8_t x, int8_t y, MoveDirection dir) {
  std::ostringstream oss;
  oss << __func__ << " (x=" << +x << ",y=" << +y << ") -> " << Stringify(dir);
  logger_.Log(Logger::Level::kInfo, oss.str() + std::string{'\n'});

  if (!CanMove(x, y, dir)) {
    return false;
  }

  if (CanTake(side_to_move_)) {
    return false;
  }

  auto cmd = Command::Create<MoveCommand>(*this, *this, Coord{x, y}, dir);
  auto p_cmd = cmd.get();
  if (options_.has_history) {
    history_.emplace_back(std::move(cmd));
  }
  p_cmd->Execute();

  auto comp = GetComputerToMove();
  if (comp) {
    comp->Proceed();
  }

  return true;
}

bool Engine::CanTake(Side side) const {
  for (const auto& row : board_) {
    for (auto piece_it = row.begin(); piece_it != row.end(); ++piece_it) {
      auto& piece = *piece_it;
      if (piece && (piece->side() == side)) {
        Coord coord = piece_it.GetCoord();
        if (CanTake(coord.x(), coord.y())) {
          return true;
        }
      }
    }
  }
  return false;
}

bool Engine::CanTake(int8_t x, int8_t y) const {
  return (CanTake(x, y, MoveDirection::kTopLeft) ||
          CanTake(x, y, MoveDirection::kTopRight) ||
          CanTake(x, y, MoveDirection::kBottomLeft) ||
          CanTake(x, y, MoveDirection::kBottomRight));
}

bool Engine::CanTake(int8_t x, int8_t y, MoveDirection dir) const {
  if (MoveDirection::kUnset == dir) {
    return false;
  }

  try {
    Coord coord{x, y};
    const Piece* const& pbeg = board_(coord);
    if (!pbeg) {
      return false;
    }

    if (pbeg->side() != side_to_move_) {
      return false;
    }

    if (pbeg->level() == Level::kMan) {
      if (pbeg->side() == Side::kLight) {
        if ((dir == MoveDirection::kBottomLeft) ||
            (dir == MoveDirection::kBottomRight)) {
          return false;
        }
      } else /*Side::kDark*/ {
        if ((dir == MoveDirection::kTopLeft) ||
            (dir == MoveDirection::kTopRight)) {
          return false;
        }
      }
    }

    const Piece* const& pmid = board_(static_cast<int8_t>(coord.x() + Dx(dir)),
                                      static_cast<int8_t>(coord.y() + Dy(dir)));
    if (!pmid || (pmid->side() == pbeg->side())) {
      return false;
    }

    Coord take_pos{static_cast<int8_t>(coord.x() + 2 * Dx(dir)),
                   static_cast<int8_t>(coord.y() + 2 * Dy(dir))};
    const Piece* const& pend = board_(take_pos);
    if (pend) {
      return false;
    }
  } catch (std::exception&) {
    return false;
  }
  return true;
}

bool Engine::Take(int8_t x, int8_t y, MoveDirection dir) {
  std::ostringstream oss;
  oss << __func__ << " (x=" << +x << ",y=" << +y << ") -> " << Stringify(dir);
  logger_.Log(Logger::Level::kInfo, oss.str() + std::string{'\n'});

  if (!CanTake(x, y, dir)) {
    return false;
  }

  auto cmd = Command::Create<TakeCommand>(*this, *this, Coord{x, y}, dir, true);
  auto p_cmd = cmd.get();
  if (options_.has_history) {
    history_.emplace_back(std::move(cmd));
  }
  p_cmd->Execute();

  auto comp = GetComputerToMove();
  if (comp) {
    comp->Proceed();
  }

  return true;
}

std::vector<HistoryItem> Engine::GetHistory(int size) {
  std::vector<HistoryItem> history_items;
  auto absz = [](int8_t sz) {
    return (sz >= 0) ? sz : -sz;
  };
  size = (size < 0) ? absz(size) : (size == 0) ? history_.size() : size;
  size = std::min(size, static_cast<int>(history_.size()));
  history_items.reserve(size + 1);
  auto it = [this, &size] {
    int iend = history_.size() - size;
    auto it = history_.begin();
    for (int i{}; i < iend; ++i, ++it) {}
    return it;
  }();
  for (; it != history_.end(); ++it) {
    history_items.emplace_back((*it)->coord().x(), (*it)->coord().y(),
                               (*it)->direction(), (*it)->side_to_move(),
                               (*it)->num_kings(), (*it)->num_men(),
                               (*it)->num_seq_moves(),
                               (*it)->num_promo_paths());
  }
  const std::map<Side, int8_t> num_kings{
    {Side::kLight,
     static_cast<int8_t>(GetCount(Side::kLight, Level::kKing).size())},
    {Side::kDark,
     static_cast<int8_t>(GetCount(Side::kDark, Level::kKing).size())}};
  const std::map<Side, int8_t> num_men{
    {Side::kLight,
     static_cast<int8_t>(GetCount(Side::kLight, Level::kMan).size())},
    {Side::kDark,
     static_cast<int8_t>(GetCount(Side::kDark, Level::kMan).size())}
  };
  const std::map<Side, int8_t> num_promo_paths{
    {Side::kLight, GetPromoPaths(Side::kLight)},
    {Side::kDark, GetPromoPaths(Side::kDark)}
  };
  history_items.emplace_back(0, 0, MoveDirection::kUnset,
                             side_to_move_, num_kings,
                             num_men, num_seq_moves_, num_promo_paths);
  return history_items;
}

Engine::Engine(const Observer& observer, const Logger& logger)
    : observer_{const_cast<Engine::Observer&>(observer)},
      logger_{const_cast<Engine::Logger&>(logger)} {}

std::pair<bool, std::list<Command::Ptr>> Engine::Proceed() {
  std::list<Command::Ptr> commands;
  bool can_proceed{true};
  auto can_move = this->CanMove(side_to_move_);
  auto can_take = this->CanTake(side_to_move_);
  if (can_move || can_take) {
    if (!can_take && (GameType::kAnalysis != options_.game_type)) {
      auto count = GetCount(side_to_move_);
      if (count.size() == 1) {
        commands = GetAutoCommands(count.front());
        if (commands.empty()) {
          can_proceed = false;
        }
      } else {
        commands = GetMoves(side_to_move_);
      }
    }
    if (can_proceed) {
      observer_.OnGameUpdated(side_to_move_, static_cast<Board::Data>(board_));
    }
  } else {
    can_proceed = false;
  }

  if (can_proceed && can_take) {
    commands = GetTakes();
  }
  return {can_proceed, std::move(commands)};
}

std::list<Coord> Engine::GetCount() const {
  std::list<Coord> count;
  auto curr_count = GetCount(side_to_move_);
  count.insert(count.end(), curr_count.begin(), curr_count.end());
  auto next_count = GetCount(Reverse(side_to_move_));
  count.insert(count.end(), next_count.begin(), next_count.end());
  return count;
}

std::list<Coord> Engine::GetCount(Side side) const {
  std::list<Coord> count;
  auto men_count = GetCount(side, Level::kMan);
  count.insert(count.end(), men_count.begin(), men_count.end());
  auto kings_count = GetCount(side, Level::kKing);
  count.insert(count.end(), kings_count.begin(), kings_count.end());
  return count;
}

std::list<Coord> Engine::GetCount(Side side, Level level) const {
  std::list<Coord> count;
  for (const auto& row : board_) {
    for (auto piece_it = row.begin(); piece_it != row.end(); ++piece_it) {
      auto& piece = *piece_it;
      if (piece && (piece->side() == side) && (piece->level() == level)) {
        Coord coord = piece_it.GetCoord();
        count.push_back(coord);
      }
    }
  }
  return count;
}

std::list<Command::Ptr> Engine::GetAutoCommands(const Coord& coord) const {
  Client cli{side_to_move_};
  Engine eng{cli, cli};
  Options opts{GameType::kAnalysis, side_to_move_,
               static_cast<Board::Data>(board_), num_seq_moves_, false};
  eng.StartGame(&opts);
  std::list<Command::Ptr> commands;
  for (auto& cmd : eng.GetMoves(coord)) {
    cmd->Execute();
    if (Reverse(cli.side_to_move()) == cli.side_that_wins()) {
    } else if (eng.CanTake(eng.side_to_move_)) {
    } else {
      commands.emplace_back(
        Command::Create<MoveCommand>(*this, *this, cmd->coord(),
                                     cmd->direction()));
    }
    cli.Reset();
    cmd->Revert();
  }
  return commands;
}

std::list<Command::Ptr> Engine::GetMoves(Side side) const {
  std::list<Command::Ptr> moves;
  for (const auto& row : board_) {
    for (auto piece_it = row.begin(); piece_it != row.end(); ++piece_it) {
      auto& piece = *piece_it;
      if (piece && (piece->side() == side)) {
        Coord coord = piece_it.GetCoord();
        auto moves_by_pos = GetMoves(coord);
        for (auto& move : moves_by_pos) {
          moves.push_back(std::move(move));
        }
      }
    }
  }
  return moves;
}

std::list<Command::Ptr> Engine::GetMoves(const Coord& pos) const {
  std::list<Command::Ptr> moves;
  if (CanMove(pos.x(), pos.y(), MoveDirection::kTopLeft)) {
    moves.emplace_back(
      Command::Create<MoveCommand>(*this, *this, pos,
                                   MoveDirection::kTopLeft));
  }
  if (CanMove(pos.x(), pos.y(), MoveDirection::kTopRight)) {
    moves.emplace_back(
      Command::Create<MoveCommand>(*this, *this, pos,
                                   MoveDirection::kTopRight));
  }
  if (CanMove(pos.x(), pos.y(), MoveDirection::kBottomLeft)) {
    moves.emplace_back(
      Command::Create<MoveCommand>(*this, *this, pos,
                                   MoveDirection::kBottomLeft));
  }
  if (CanMove(pos.x(), pos.y(), MoveDirection::kBottomRight)) {
    moves.emplace_back(
      Command::Create<MoveCommand>(*this, *this, pos,
                                   MoveDirection::kBottomRight));
  }
  return moves;
}

std::list<Command::Ptr> Engine::GetTakes() const {
  std::list<Command::Ptr> takes;
  for (int8_t x{1}; x <= Config::kBoardSize; ++x) {
    for (int8_t y{1}; y <= Config::kBoardSize; ++y) {
      auto takes_by_pos = GetTakes(Coord{x, y});
      for (auto& take : takes_by_pos) {
        takes.push_back(std::move(take));
      }
    }
  }
  return takes;
}

std::list<Command::Ptr> Engine::GetTakes(Side side) const {
  std::list<Command::Ptr> takes;
  for (const auto& row : board_) {
    for (auto piece_it = row.begin(); piece_it != row.end(); ++piece_it) {
      auto& piece = *piece_it;
      if (piece && (piece->side() == side)) {
        Coord coord = piece_it.GetCoord();
        auto takes_by_pos = GetTakes(coord);
        for (auto& take : takes_by_pos) {
          takes.push_back(std::move(take));
        }
      }
    }
  }
  return takes;
}

int Engine::GetTakesCount(Side side) const {
  int count{};
  for (const auto& row : board_) {
    for (auto piece_it = row.begin(); piece_it != row.end(); ++piece_it) {
      auto& piece = *piece_it;
      if (piece && (piece->side() == side)) {
        Coord coord = piece_it.GetCoord();
        if (CanTake(coord.x(), coord.y(),
                    MoveDirection::kTopLeft)) {
          ++count;
        }
        if (CanTake(coord.x(), coord.y(),
                    MoveDirection::kTopRight)) {
          ++count;
        }
        if (CanTake(coord.x(), coord.y(),
                    MoveDirection::kBottomLeft)) {
          ++count;
        }
        if (CanTake(coord.x(), coord.y(),
                    MoveDirection::kBottomRight)) {
          ++count;
        }
      }
    }
  }
  return count;
}

std::list<Command::Ptr> Engine::GetTakes(const Coord& pos) const {
  std::list<Command::Ptr> takes;
  if (CanTake(pos.x(), pos.y(), MoveDirection::kTopLeft)) {
    takes.emplace_back(
      Command::Create<TakeCommand>(*this, *this, pos, MoveDirection::kTopLeft,
                                   true));
  }
  if (CanTake(pos.x(), pos.y(), MoveDirection::kTopRight)) {
    takes.emplace_back(
      Command::Create<TakeCommand>(*this, *this, pos,
                                   MoveDirection::kTopRight, true));
  }
  if (CanTake(pos.x(), pos.y(), MoveDirection::kBottomLeft)) {
    takes.emplace_back(
      Command::Create<TakeCommand>(*this, *this, pos,
                                   MoveDirection::kBottomLeft, true));
  }
  if (CanTake(pos.x(), pos.y(), MoveDirection::kBottomRight)) {
    takes.emplace_back(
      Command::Create<TakeCommand>(*this, *this, pos,
                                   MoveDirection::kBottomRight, true));
  }
  return takes;
}

int8_t Engine::GetPromoPaths(Side side) {
  int8_t count{};
  Side last_side_to_move = side_to_move_;
  if (side_to_move_ != side) {
    side_to_move_ = side;
  }
  for (const auto& row : board_) {
    for (auto piece_it = row.begin(); piece_it != row.end(); ++piece_it) {
      auto& piece = *piece_it;
      if (piece && (piece->side() == side) && (piece->level() == Level::kMan)) {
        Coord coord = piece_it.GetCoord();
        if (FindPromoPath(coord, MoveDirection::kTopLeft)) {
          ++count;
        }
        if (FindPromoPath(coord, MoveDirection::kTopRight)) {
          ++count;
        }
        if (FindPromoPath(coord, MoveDirection::kBottomLeft)) {
          ++count;
        }
        if (FindPromoPath(coord, MoveDirection::kBottomRight)) {
          ++count;
        }
      }
    }
  }
  if (side_to_move_ != last_side_to_move) {
    side_to_move_ = last_side_to_move;
  }
  return count;
}

bool Engine::FindPromoPath(const Coord& prev, MoveDirection dir) {
  if (!CanMove(prev.x(), prev.y(), dir)) {
    return false;
  }
  Coord next{static_cast<int8_t>(prev.x() + Dx(dir)),
              static_cast<int8_t>(prev.y() + Dy(dir))};
  if ((next.x() == 1) || (next.x() == Config::kBoardSize)) {
    return true;
  }
  auto& pprev = board_(prev.x(), prev.y());
  auto& pnext = board_(next.x(), next.y());
  std::swap(pprev, pnext);

  side_to_move_ = Reverse(side_to_move_);
  if (CanTake(side_to_move_)) {
    std::swap(pprev, pnext);
    side_to_move_ = Reverse(side_to_move_);
    return false;
  }

  side_to_move_ = Reverse(side_to_move_);
  bool found = false;
  for (auto& m : GetMoves(next)) {
    if (FindPromoPath(m->coord(), m->direction())) {
      found = true;
      break;
    }
  }
  std::swap(pprev, pnext);
  return found;
}

void Engine::BeforeMove(Side side_to_move, int8_t num_seq_moves) {
  if ((side_to_move != Side::kUnset) && (side_to_move != Side::kNeutral)) {
    side_to_move_ = side_to_move;
    num_seq_moves_ = num_seq_moves;
  }
  observer_.OnGameUpdated(side_to_move_, static_cast<Board::Data>(board_));
}

std::list<Command::Ptr> Engine::AfterMove() {
  if (++num_seq_moves_ == kMaxNumSeqMoves) {
    observer_.OnGameUpdated(Side::kUnset, static_cast<Board::Data>(board_));
    OnGameEnded(Side::kNeutral);
    side_to_move_ = Side::kUnset;
    return {};
  }

  side_to_move_ = Reverse(side_to_move_);

  auto to_proceed = Proceed();
  if (!to_proceed.first) {
    observer_.OnGameUpdated(Side::kUnset, static_cast<Board::Data>(board_));
    OnGameEnded(Reverse(side_to_move_));
    side_to_move_ = Side::kUnset;
  }
  return std::move(to_proceed.second);
}

void Engine::BeforeTake(Side side_to_move, int8_t num_seq_moves) {
  if ((side_to_move != Side::kUnset) && (side_to_move != Side::kNeutral)) {
    side_to_move_ = side_to_move;
    num_seq_moves_ = num_seq_moves;
  }
  observer_.OnGameUpdated(side_to_move_, static_cast<Board::Data>(board_));
}

std::list<Command::Ptr> Engine::AfterTake() {
  side_to_move_ = Reverse(side_to_move_);

  auto to_proceed = Proceed();
  if (!to_proceed.first) {
    observer_.OnGameUpdated(Side::kUnset, static_cast<Board::Data>(board_));
    OnGameEnded(Reverse(side_to_move_));
    side_to_move_ = Side::kUnset;
  }
  return std::move(to_proceed.second);
}

std::list<Command::Ptr> Engine::AfterTake(const Coord& coord) {
  observer_.OnGameUpdated(side_to_move_, static_cast<Board::Data>(board_));
  num_seq_moves_ = 0;
  return GetTakes(coord);
}

void Engine::SetupComputers() {
  if (GameType::kUnset == options_.game_type) {
    std::ostringstream oss;
    oss << "(" << Stringify(options_.game_type) << ")";
    throw std::invalid_argument{"Invalid GameType - " + oss.str()};
  }

  switch (options_.game_type) {
    case GameType::kHumanComputer:
      computer1_ = Computer::Create(Side::kDark, *this);
      computer2_.reset();
      break;
    case GameType::kComputerHuman:
      computer1_ = Computer::Create(Side::kLight, *this);
      computer2_.reset();
      break;
    case GameType::kComputerComputer:
      computer1_ = Computer::Create(Side::kLight, *this);
      computer2_ = Computer::Create(Side::kDark, *this);
      break;
    default:
      computer1_.reset();
      computer2_.reset();
      break;
  }
}

Computer* Engine::GetComputerToMove() const {
  for (auto& c : {computer1_.get(), computer2_.get()}) {
    if (c && (c->side() == side_to_move_)) {
      return c;
    }
  }
  return nullptr;
}

void Engine::OnGameEnded(Side side_that_wins) {
  side_that_wins_ = side_that_wins;
  observer_.OnGameEnded(side_that_wins);
}

}  // namespace checkers_style_game
