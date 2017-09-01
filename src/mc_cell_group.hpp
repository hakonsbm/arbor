#pragma once

#include <cstdint>
#include <functional>
#include <iterator>
#include <unordered_map>
#include <vector>

#include <algorithms.hpp>
#include <cell_group.hpp>
#include <cell.hpp>
#include <common_types.hpp>
#include <event_binner.hpp>
#include <event_queue.hpp>
#include <recipe.hpp>
#include <sampler_map.hpp>
#include <sampling.hpp>
#include <spike.hpp>
#include <util/debug.hpp>
#include <util/filter.hpp>
#include <util/partition.hpp>
#include <util/range.hpp>
#include <util/unique_any.hpp>

#include <profiling/profiler.hpp>

namespace nest {
namespace mc {

template <typename LoweredCell>
class mc_cell_group: public cell_group {
public:
    using lowered_cell_type = LoweredCell;
    using value_type = typename lowered_cell_type::value_type;
    using size_type  = typename lowered_cell_type::value_type;

    mc_cell_group() = default;

    mc_cell_group(std::vector<cell_gid_type> gids, const recipe& rec):
        gids_(std::move(gids))
    {
        // Build lookup table for gid to local index.
        for (auto i: util::make_span(0, gids_.size())) {
            gid_index_map_[gids_[i]] = i;
        }

        // Create lookup structure for target ids.
        build_target_handle_partition(rec);
        std::size_t n_targets = target_handle_divisions_.back();

        // Pre-allocate space to store handles, probe map.
        auto n_probes = util::sum_by(gids_, [&rec](cell_gid_type i) { return rec.num_probes(i); });
        probe_map_.reserve(n_probes);
        target_handles_.reserve(n_targets);

        // Construct cell implementation, retrieving handles and maps. 
        lowered_.initialize(gids_, rec, target_handles_, probe_map_);

        // Create a list of the global identifiers for the spike sources
        for (auto source_gid: gids_) {
            for (cell_lid_type lid = 0; lid<rec.num_sources(source_gid); ++lid) {
                spike_sources_.push_back({source_gid, lid});
            }
        }

        spike_sources_.shrink_to_fit();
    }

    cell_kind get_cell_kind() const override {
        return cell_kind::cable1d_neuron;
    }

    void reset() override {
        spikes_.clear();
        events_.clear();
        reset_samplers();
        binner_.reset();
        lowered_.reset();
    }

    void set_binning_policy(binning_kind policy, time_type bin_interval) override {
        binner_ = event_binner(policy, bin_interval);
    }

    void advance(time_type tfinal, time_type dt) override {
        EXPECTS(lowered_.state_synchronized());
        time_type tstart = lowered_.min_time();

        // Bin pending events and enqueue on lowered state.
        time_type ev_min_time = lowered_.max_time(); // (but we're synchronized here)
        while (auto ev = events_.pop_if_before(tfinal)) {
            auto handle = get_target_handle(ev->target);
            auto binned_ev_time = binner_.bin(ev->target.gid, ev->time, ev_min_time);
            lowered_.add_event(binned_ev_time, handle, ev->weight);
        }

        lowered_.setup_integration(tfinal, dt);

        // Set up sample event queue.
        struct sampler_entry {
            typename lowered_cell_type::probe_handle handle;
            probe_tag tag;
            cell_member_type probe_id;
            sampler_function sampler;
        };
        std::vector<sampler_entry> samplers;

        for (auto &assoc: sampler_map_) {
            auto ts = assoc.sched.events(tstart, tfinal);
            if (ts.empty()) {
                continue;
            }

            for (auto p: assoc.probe_ids) {
                EXPECTS(probe_map_.count(p)>0);
                sample_event::size_type idx = samplers.size();
                auto pinfo = probe_map_[p];

                samplers.push_back({pinfo.handle, pinfo.tag, p, assoc.sampler});

                for (auto t: ts) {
                    sample_events_.push({idx, t});
                }
            }
        }

        util::optional<time_type> first_sample_time = sample_events_.time_if_before(tfinal);
        std::vector<sample_event> requeue_sample_events;
        while (!lowered_.integration_complete()) {
            // Take any pending samples.
            // TODO: Placeholder: this will be replaced by a backend polling implementation.

            if (first_sample_time) {
                PE("sampling");
                time_type cell_max_time = lowered_.max_time();

                requeue_sample_events.clear();
                while (auto m = sample_events_.pop_if_before(cell_max_time)) {
                    auto& s = samplers[m->sampler_index];
                    EXPECTS((bool)s.sampler);

                    time_type cell_time = lowered_.time(gid_to_index(s.probe_id.gid));
                    if (cell_time<m->time) {
                        // This cell hasn't reached this sample time yet.
                        requeue_sample_events.push_back(*m);
                    }
                    else {
                        const double value = lowered_.probe(s.handle);
                        sample_record record = {cell_time, &value};
                        s.sampler(s.probe_id, s.tag, 1u, &record);
                    }
                }
                for (auto& ev: requeue_sample_events) {
                    sample_events_.push(std::move(ev));
                }
                first_sample_time = sample_events_.time_if_before(tfinal);
                PL();
            }

            // Ask lowered_ cell to integrate 'one step', delivering any
            // events accordingly.
            // TODO: Placeholder: with backend polling for samplers, we will
            // request that the lowered cell perform the integration all the
            // way to tfinal.

            lowered_.step_integration();

            if (util::is_debug_mode() && !lowered_.is_physical_solution()) {
                std::cerr << "warning: solution out of bounds  at (max) t "
                          << lowered_.max_time() << " ms\n";
            }
        }

        // Copy out spike voltage threshold crossings from the back end, then
        // generate spikes with global spike source ids. The threshold crossings
        // record the local spike source index, which must be converted to a
        // global index for spike communication.
        PE("events");
        for (auto c: lowered_.get_spikes()) {
            spikes_.push_back({spike_sources_[c.index], time_type(c.time)});
        }
        // Now that the spikes have been generated, clear the old crossings
        // to get ready to record spikes from the next integration period.
        lowered_.clear_spikes();
        PL();
    }

    void enqueue_events(const std::vector<postsynaptic_spike_event>& events) override {
        for (auto& e: events) {
            events_.push(e);
        }
    }

    const std::vector<spike>& spikes() const override {
        return spikes_;
    }

    void clear_spikes() override {
        spikes_.clear();
    }

    const std::vector<cell_member_type>& spike_sources() const {
        return spike_sources_;
    }

    void add_sampler(sampler_association_handle h, cell_member_predicate probe_ids,
                     schedule sched, sampler_function fn, sampling_policy policy) override
    {
        std::vector<cell_member_type> probeset =
            util::assign_from(util::filter(util::keys(probe_map_), probe_ids));

        if (!probeset.empty()) {
            sampler_map_.add(h, sampler_association{std::move(sched), std::move(fn), std::move(probeset)});
        }
    }

    void remove_sampler(sampler_association_handle h) override {
        sampler_map_.remove(h);
    }

    void remove_all_samplers() override {
        sampler_map_.clear();
    }

private:
    // List of the gids of the cells in the group.
    std::vector<cell_gid_type> gids_;

    // Hash table for converting gid to local index
    std::unordered_map<cell_gid_type, cell_gid_type> gid_index_map_;

    // The lowered cell state (e.g. FVM) of the cell.
    lowered_cell_type lowered_;

    // Spike detectors attached to the cell.
    std::vector<cell_member_type> spike_sources_;

    // Spikes that are generated.
    std::vector<spike> spikes_;

    // Event time binning manager.
    event_binner binner_;

    // Pending events to be delivered.
    event_queue<postsynaptic_spike_event> events_;

    // Pending samples to be taken.
    event_queue<sample_event> sample_events_;

    // Handles for accessing lowered cell.
    using target_handle = typename lowered_cell_type::target_handle;
    std::vector<target_handle> target_handles_;

    // Maps probe ids to probe handles (from lowered cell) and tags (from probe descriptions).
    using probe_handle = typename lowered_cell_type::probe_handle;
    probe_association_map<probe_handle> probe_map_;

    // Collection of samplers to be run against probes in this group.
    sampler_association_map sampler_map_;

    // Lookup table for target ids -> local target handle indices.
    std::vector<std::size_t> target_handle_divisions_;

    // Build handle index lookup tables.
    void build_target_handle_partition(const recipe& rec) {
        util::make_partition(target_handle_divisions_,
            util::transform_view(gids_, [&rec](cell_gid_type i) { return rec.num_targets(i); }));
    }

    // Get target handle from target id.
    target_handle get_target_handle(cell_member_type id) const {
        return target_handles_[target_handle_divisions_[gid_to_index(id.gid)]+id.index];
    }

    void reset_samplers() {
        // clear all pending sample events and reset to start at time 0
        sample_events_.clear();
        for (auto &assoc: sampler_map_) {
            assoc.sched.reset();
        }
    }

    cell_gid_type gid_to_index(cell_gid_type gid) const {
        auto it = gid_index_map_.find(gid);
        EXPECTS(it!=gid_index_map_.end());
        return it->second;
    }
};

} // namespace mc
} // namespace nest