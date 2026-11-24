// Leptid — undo/redo command stack.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace leptid {

class Command {
public:
    virtual ~Command() {}
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual std::string name() const = 0;
};

// Generic command from two closures — covers most editing operations.
class FuncCommand : public Command {
public:
    FuncCommand(const std::string& n, std::function<void()> u, std::function<void()> r)
        : n_(n), undoF_(std::move(u)), redoF_(std::move(r)) {}
    void undo() override { undoF_(); }
    void redo() override { redoF_(); }
    std::string name() const override { return n_; }
private:
    std::string n_;
    std::function<void()> undoF_, redoF_;
};

class CommandStack {
public:
    // Executes the command's redo and pushes it (standard edit flow).
    void push(std::unique_ptr<Command> c) {
        c->redo();
        undo_.push_back(std::move(c));
        redo_.clear();
        if (undo_.size() > 200) undo_.erase(undo_.begin()); // bounded history
    }
    // Push an already-executed change without re-running redo (e.g. live edits).
    void pushExecuted(std::unique_ptr<Command> c) {
        undo_.push_back(std::move(c));
        redo_.clear();
        if (undo_.size() > 200) undo_.erase(undo_.begin());
    }
    bool undo() {
        if (undo_.empty()) return false;
        auto c = std::move(undo_.back());
        undo_.pop_back();
        c->undo();
        redo_.push_back(std::move(c));
        return true;
    }
    bool redo() {
        if (redo_.empty()) return false;
        auto c = std::move(redo_.back());
        redo_.pop_back();
        c->redo();
        undo_.push_back(std::move(c));
        return true;
    }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    std::string nextUndoName() const { return undo_.empty() ? "" : undo_.back()->name(); }
    void clear() { undo_.clear(); redo_.clear(); }

private:
    std::vector<std::unique_ptr<Command>> undo_, redo_;
};

} // namespace leptid
