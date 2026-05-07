#include "explorer_detail_actions.hpp"

#include "explorer_details.hpp"
#include "explorer_render.hpp"
#include "explorer_style.hpp"

#include <algorithm>

namespace tundraux::explorer {

void beginShowDetails(ExplorerState& state) {
    if (state.entries.empty() || state.cursor >= state.entries.size()) {
        state.message = redMessage("Nothing selected");
        return;
    }

    state.detailLines = buildDetailLines(state);
    state.detailName = state.entries[state.cursor].name;
    state.detailScroll = 0;
    state.showDetails = true;
}

std::size_t maxDetailScroll(const ExplorerState& state) {
    const COORD size = consoleSize();
    const std::size_t height = std::max<int>(size.Y, 18);
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
