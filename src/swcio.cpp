#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>
#include <unordered_set>

#include <swcio.hpp>

namespace nestmc
{
namespace io
{

//
// cell_record implementation
//
void cell_record::renumber(id_type new_id, std::map<id_type, id_type> &idmap)
{
    auto old_id = id_;
    id_ = new_id;

    // Obtain parent_id from the map
    auto new_parent_id = idmap.find(parent_id_);
    if (new_parent_id != idmap.end()) {
        parent_id_ = new_parent_id->second;
    }

    check_consistency();
    idmap.insert(std::make_pair(old_id, new_id));
}

void cell_record::check_consistency() const
{
    // Check cell type as well; enum's do not offer complete type safety,
    // since you can cast anything that fits to its underlying type
    if (type_ < 0 || type_ > custom) {
        throw std::invalid_argument("unknown cell type");
    }

    if (id_ < 0) {
        throw std::invalid_argument("negative ids not allowed");
    }

    if (parent_id_ < -1) {
        throw std::invalid_argument("parent_id < -1 not allowed");
    }

    if (parent_id_ >= id_) {
        throw std::invalid_argument("parent_id >= id is not allowed");
    }

    if (r_ < 0) {
        throw std::invalid_argument("negative radii are not allowed");
    }
}

std::istream &operator>>(std::istream &is, cell_record &cell)
{
    swc_parser parser;
    parser.parse_record(is, cell);
    return is;
}


std::ostream &operator<<(std::ostream &os, const cell_record &cell)
{
    // output in one-based indexing
    os << cell.id_+1 << " "
       << cell.type_ << " "
       << std::setprecision(7) << cell.x_ << " "
       << std::setprecision(7) << cell.y_ << " "
       << std::setprecision(7) << cell.z_ << " "
       << std::setprecision(7) << cell.r_ << " "
       << ((cell.parent_id_ == -1) ? cell.parent_id_ : cell.parent_id_+1);

    return os;
}


//
// Utility functions
//

bool starts_with(const std::string &str, const std::string &prefix)
{
    return (str.find(prefix) == 0);
}

void check_parse_status(const std::istream &is, const swc_parser &parser)
{
    if (is.fail()) {
        // If we try to read past the eof; fail bit will also be set
        throw swc_parse_error("could not parse value", parser.lineno());
    }
}

template<typename T>
T parse_value_strict(std::istream &is, const swc_parser &parser)
{
    T val;
    check_parse_status(is >> val, parser);

    // everything's fine
    return val;
}

// specialize parsing for cell types
template<>
cell_record::kind parse_value_strict(std::istream &is, const swc_parser &parser)
{
    cell_record::id_type val;
    check_parse_status(is >> val, parser);

    // Let cell_record's constructor check for the type validity
    return static_cast<cell_record::kind>(val);
}

//
// swc_parser implementation
//

std::istream &swc_parser::parse_record(std::istream &is, cell_record &cell)
{
    while (!is.eof() && !is.bad()) {
        // consume empty and comment lines first
        std::getline(is, linebuff_);
        ++lineno_;
        if (!linebuff_.empty() && !starts_with(linebuff_, comment_prefix_))
            break;
    }

    if (is.bad()) {
        // let the caller check for such events
        return is;
    }

    if (is.eof() &&
        (linebuff_.empty() || starts_with(linebuff_, comment_prefix_))) {
        // last line is either empty or a comment; don't parse anything
        return is;
    }

    if (is.fail()) {
        throw swc_parse_error("too long line detected", lineno_);
    }

    std::istringstream line(linebuff_);
    try {
        cell = parse_record(line);
    } catch (std::invalid_argument &e) {
        // Rethrow as a parse error
        throw swc_parse_error(e.what(), lineno_);
    }

    return is;
}

cell_record swc_parser::parse_record(std::istringstream &is)
{
    auto id = parse_value_strict<int>(is, *this);
    auto type = parse_value_strict<cell_record::kind>(is, *this);
    auto x = parse_value_strict<float>(is, *this);
    auto y = parse_value_strict<float>(is, *this);
    auto z = parse_value_strict<float>(is, *this);
    auto r = parse_value_strict<float>(is, *this);
    auto parent_id = parse_value_strict<cell_record::id_type>(is, *this);

    // Convert to zero-based, leaving parent_id as-is if -1
    if (parent_id != -1) {
        parent_id--;
    }

    return cell_record(type, id-1, x, y, z, r, parent_id);
}


cell_record_range_clean::cell_record_range_clean(std::istream &is)
{
    std::unordered_set<cell_record::id_type> ids;

    std::size_t          num_trees = 0;
    cell_record::id_type last_id   = -1;
    bool                 needsort  = false;

    cell_record curr_cell;
    for (auto c : swc_get_records<swc_io_raw>(is)) {
        if (c.parent() == -1 && ++num_trees > 1) {
            // only a single tree is allowed
            break;
        }

        auto inserted = ids.insert(c.id());
        if (inserted.second) {
            // not a duplicate; insert cell
            cells_.push_back(c);
            if (!needsort && c.id() < last_id) {
                needsort = true;
            }

            last_id = c.id();
        }
    }

    if (needsort) {
        std::sort(cells_.begin(), cells_.end());
    }

    // Renumber cells if necessary
    std::map<cell_record::id_type, cell_record::id_type> idmap;
    cell_record::id_type next_id = 0;
    for (auto &c : cells_) {
        if (c.id() != next_id) {
            c.renumber(next_id, idmap);
        }

        ++next_id;
    }
}

}   // end of nestmc::io
}   // end of nestmc