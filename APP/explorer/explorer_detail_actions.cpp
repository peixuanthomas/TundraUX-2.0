#include "explorer_detail_actions.hpp"

#include "explorer_details.hpp"
#include "explorer_render.hpp"
#include "explorer_style.hpp"
#include "explorer_text.hpp"
#include "backend_facade.hpp"

#include <algorithm>

namespace tundraux::explorer {
namespace {

void setAuditUser(const ExplorerState& state) {
    if (state.audit == nullptr) {
        return;
    }
    state.audit->setCurrentUser({state.usertype, state.username, "", 0});
}

void logAuditEvent(const ExplorerState& state, const std::string& category, const std::string& detail) {
    if (state.audit == nullptr) {
        return;
    }
    setAuditUser(state);
    state.audit->logEvent(category, detail);
}

}

void beginShowDetails(ExplorerState& state) {
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        state.message = redMessage("Nothing selected");
        logAuditEvent(state, "explorer", "details denied reason=nothing selected");
        return;
    }

    const FileEntry& entry = state.entries[state.cursor];
    state.detailLines = buildDetailLines(state);
    state.detailName = entry.name;
    state.detailScroll = 0;
    state.showDetails = true;
    logAuditEvent(state, "explorer", "details success path=" + pathToDisplayString(entry.path));
}

std::size_t maxDetailScroll(const ExplorerState& state) {
    const tundra_tui::Size size = consoleSize();
    const std::size_t height = std::max<int>(size.height, 18);
    const std::size_t rows = detailVisibleRows(height);
    return state.detailLines.size() > rows ? state.detailLines.size() - rows : 0;
}

void scrollDetailsUp(ExplorerState& state) {
    if (state.detailScroll > 0) {
        --state.detailScroll;
    }
}

void scrollDetailsDown(ExplorerState& state) {
    state.detailScroll = std::min(state.detailScroll + 1, maxDetailScroll(state));
}

}
