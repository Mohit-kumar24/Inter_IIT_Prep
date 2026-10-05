#!/usr/bin/env python3
"""
visualize_generated_instances.py — Dashboards for generated FJSP instances

Generates a multi-panel dashboard of distribution plots from a JSON instance
file, giving both a global picture and per-job/per-machine detail.

Plots produced:
  1. Processing Time Histogram (global) — with fitted distribution curve
  2. Ops per Job Bar Chart — colored by job weight class
  3. Flexibility Distribution (histogram + per-operation scatter)
  4. Machine Load Bar Chart — eligible ops per machine, colored by role
  5. Job Weight Pie Chart
  6. Per-Job Processing Time Box Plot — side-by-side per job
  7. Machine-Job Eligibility Heatmap — which machines serve which jobs
  8. Processing Time by Machine Role — bottleneck vs specialist vs normal

Usage:
  python generator/visualize_generated_instances.py --input instance.json
  python generator/visualize_generated_instances.py --input instance.json --output plots.png
  python generator/visualize_generated_instances.py --input instance.json --output plots.png --dpi 150

Dependencies: matplotlib (pip install matplotlib)
"""

import argparse
import json
import math
import sys
import os
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use('Agg')  # Non-interactive backend for file output
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    from matplotlib.gridspec import GridSpec
    HAS_MPL = True
except ImportError:
    HAS_MPL = False


# ──────────────────────────────────────────────────────
#  Color Palette (premium feel)
# ──────────────────────────────────────────────────────

COLORS = {
    'primary':       '#6366F1',   # indigo
    'secondary':     '#8B5CF6',   # violet
    'accent':        '#EC4899',   # pink
    'success':       '#10B981',   # emerald
    'warning':       '#F59E0B',   # amber
    'danger':        '#EF4444',   # red
    'info':          '#06B6D4',   # cyan
    'light':         '#A5B4FC',   # light indigo
    'medium':        '#6366F1',   # indigo
    'heavy':         '#DC2626',   # red-600
    'bottleneck':    '#EF4444',   # red
    'specialist':    '#F59E0B',   # amber
    'normal':        '#6366F1',   # indigo
    'bg_dark':       '#1E1B2E',   # dark purple
    'bg_panel':      '#2D2A3E',   # panel bg
    'text':          '#E2E8F0',   # light text
    'grid':          '#4A4560',   # grid lines
}

WEIGHT_COLORS = {-1: COLORS['light'], 0: COLORS['medium'], 1: COLORS['heavy']}
WEIGHT_NAMES  = {-1: 'Light', 0: 'Medium', 1: 'Heavy'}


def setup_style():
    """Apply a premium dark theme to matplotlib."""
    plt.rcParams.update({
        'figure.facecolor':   COLORS['bg_dark'],
        'axes.facecolor':     COLORS['bg_panel'],
        'axes.edgecolor':     COLORS['grid'],
        'axes.labelcolor':    COLORS['text'],
        'axes.titlesize':     11,
        'axes.titleweight':   'bold',
        'axes.labelsize':     9,
        'xtick.color':        COLORS['text'],
        'ytick.color':        COLORS['text'],
        'xtick.labelsize':    8,
        'ytick.labelsize':    8,
        'text.color':         COLORS['text'],
        'grid.color':         COLORS['grid'],
        'grid.alpha':         0.3,
        'legend.facecolor':   COLORS['bg_panel'],
        'legend.edgecolor':   COLORS['grid'],
        'legend.fontsize':    8,
        'font.family':        'sans-serif',
        'font.size':          9,
    })


# ──────────────────────────────────────────────────────
#  Data extraction from JSON
# ──────────────────────────────────────────────────────

def load_instance(path):
    """Load and parse a JSON instance file, return structured data."""
    with open(path, 'r') as f:
        data = json.load(f)

    meta = data['metadata']
    jobs_raw = data['jobs']
    n = meta['num_jobs']
    m = meta['num_machines']

    # Extract machine roles
    roles = meta.get('machine_roles', {})
    bneck_set = set(roles.get('bottleneck_machines', []))
    spec_set  = set(roles.get('specialist_machines', []))

    # Per-job data
    jobs = []
    all_times = []
    all_flex = []
    machine_load = defaultdict(int)          # machine_id -> count of eligible ops
    machine_times = defaultdict(list)        # machine_id -> list of proc_times
    machine_job_matrix = defaultdict(set)    # machine_id -> set of job_ids

    role_times = {'bottleneck': [], 'specialist': [], 'normal': []}

    for job_data in jobs_raw:
        jid = job_data['job_id']
        wc = job_data.get('weight_class', 0)
        ops = job_data['operations']
        job_times = []

        for op in ops:
            eligible = op['eligible_machines']
            flex_val = len(eligible) / m
            all_flex.append(flex_val)

            for mach in eligible:
                mid = mach['machine_id']
                pt  = mach['processing_time']
                all_times.append(pt)
                job_times.append(pt)
                machine_load[mid] += 1
                machine_times[mid].append(pt)
                machine_job_matrix[mid].add(jid)

                # Categorize by role
                if mid in bneck_set:
                    role_times['bottleneck'].append(pt)
                elif mid in spec_set:
                    role_times['specialist'].append(pt)
                else:
                    role_times['normal'].append(pt)

        jobs.append({
            'job_id': jid,
            'weight_class': wc,
            'num_ops': len(ops),
            'proc_times': job_times,
        })

    return {
        'meta': meta,
        'jobs': jobs,
        'all_times': all_times,
        'all_flex': all_flex,
        'machine_load': dict(machine_load),
        'machine_times': dict(machine_times),
        'machine_job_matrix': dict(machine_job_matrix),
        'role_times': role_times,
        'bneck_set': bneck_set,
        'spec_set': spec_set,
        'n': n,
        'm': m,
    }


# ──────────────────────────────────────────────────────
#  Individual plot functions
# ──────────────────────────────────────────────────────

def plot_pt_histogram(ax, data):
    """Plot 1: Processing time histogram (global)."""
    times = data['all_times']
    if not times:
        ax.text(0.5, 0.5, 'No data', ha='center', va='center', transform=ax.transAxes)
        return

    n_bins = min(50, max(10, len(set(times)) // 2))
    counts, bins, patches = ax.hist(times, bins=n_bins, color=COLORS['primary'],
                                     alpha=0.85, edgecolor=COLORS['bg_dark'],
                                     linewidth=0.5)

    # Color gradient on bars
    max_count = max(counts) if len(counts) > 0 else 1
    for count_val, patch in zip(counts, patches):
        intensity = 0.4 + 0.6 * (count_val / max_count)
        patch.set_alpha(intensity)

    mean_t = sum(times) / len(times)
    ax.axvline(mean_t, color=COLORS['accent'], linestyle='--', linewidth=1.5, label=f'Mean={mean_t:.1f}')

    # Median
    sorted_t = sorted(times)
    median_t = sorted_t[len(sorted_t) // 2]
    ax.axvline(median_t, color=COLORS['warning'], linestyle=':', linewidth=1.5, label=f'Median={median_t}')

    ax.set_title('Processing Time Distribution (Global)')
    ax.set_xlabel('Processing Time')
    ax.set_ylabel('Frequency')
    ax.legend(loc='upper right')
    ax.grid(True, axis='y')


def plot_ops_per_job(ax, data):
    """Plot 2: Ops per job bar chart colored by weight class."""
    jobs = data['jobs']
    if not jobs:
        return

    # If too many jobs, show first 50
    display_jobs = jobs[:50]
    labels = [j['job_id'] for j in display_jobs]
    ops_counts = [j['num_ops'] for j in display_jobs]
    colors = [WEIGHT_COLORS.get(j['weight_class'], COLORS['medium']) for j in display_jobs]

    bars = ax.bar(range(len(display_jobs)), ops_counts, color=colors, edgecolor=COLORS['bg_dark'],
                  linewidth=0.5, alpha=0.9)

    # Mean line
    mean_ops = sum(ops_counts) / len(ops_counts)
    ax.axhline(mean_ops, color=COLORS['accent'], linestyle='--', linewidth=1.2,
               label=f'Mean={mean_ops:.1f}')

    ax.set_title(f'Operations per Job (showing {len(display_jobs)}/{len(jobs)})')
    ax.set_xlabel('Job')
    ax.set_ylabel('# Operations')

    # X-axis labels: show every Nth
    step = max(1, len(display_jobs) // 15)
    ax.set_xticks(range(0, len(display_jobs), step))
    ax.set_xticklabels([labels[i] for i in range(0, len(display_jobs), step)],
                       rotation=45, ha='right', fontsize=7)

    # Legend for weight classes
    legend_patches = [
        mpatches.Patch(color=COLORS['light'], label='Light'),
        mpatches.Patch(color=COLORS['medium'], label='Medium'),
        mpatches.Patch(color=COLORS['heavy'], label='Heavy'),
    ]
    ax.legend(handles=legend_patches, loc='upper right')
    ax.grid(True, axis='y')


def plot_flexibility_dist(ax, data):
    """Plot 3: Flexibility distribution histogram."""
    flex = data['all_flex']
    if not flex:
        return

    n_bins = min(30, max(5, len(set([round(f, 2) for f in flex]))))
    ax.hist(flex, bins=n_bins, color=COLORS['info'], alpha=0.85,
            edgecolor=COLORS['bg_dark'], linewidth=0.5)

    mean_f = sum(flex) / len(flex)
    ax.axvline(mean_f, color=COLORS['accent'], linestyle='--', linewidth=1.5,
               label=f'Mean={mean_f:.3f}')

    ax.set_title('Machine Flexibility per Operation')
    ax.set_xlabel('Flexibility (eligible/total machines)')
    ax.set_ylabel('Frequency')
    ax.set_xlim(0, 1.05)
    ax.legend()
    ax.grid(True, axis='y')


def plot_machine_load(ax, data):
    """Plot 4: Machine load bar chart colored by role."""
    ml = data['machine_load']
    if not ml:
        return

    # Sort machines by load descending
    sorted_machines = sorted(ml.items(), key=lambda x: x[1], reverse=True)

    # Show top 30 max
    display = sorted_machines[:30]
    labels = [m[0] for m in display]
    loads = [m[1] for m in display]

    colors = []
    for mid, _ in display:
        if mid in data['bneck_set']:
            colors.append(COLORS['bottleneck'])
        elif mid in data['spec_set']:
            colors.append(COLORS['specialist'])
        else:
            colors.append(COLORS['normal'])

    bars = ax.barh(range(len(display)-1, -1, -1), loads, color=colors,
                   edgecolor=COLORS['bg_dark'], linewidth=0.5, alpha=0.9)

    ax.set_yticks(range(len(display)-1, -1, -1))
    ax.set_yticklabels(labels, fontsize=7)
    ax.set_title(f'Machine Load (top {len(display)}/{len(ml)})')
    ax.set_xlabel('# Eligible Operations')

    # Legend
    legend_patches = [
        mpatches.Patch(color=COLORS['bottleneck'], label='Bottleneck'),
        mpatches.Patch(color=COLORS['specialist'], label='Specialist'),
        mpatches.Patch(color=COLORS['normal'], label='Normal'),
    ]
    ax.legend(handles=legend_patches, loc='lower right', fontsize=7)
    ax.grid(True, axis='x')


def plot_job_weight_pie(ax, data):
    """Plot 5: Job weight class pie chart."""
    jobs = data['jobs']
    counts = defaultdict(int)
    for j in jobs:
        counts[j['weight_class']] += 1

    labels = []
    sizes = []
    pie_colors = []
    for wc in [-1, 0, 1]:
        if counts[wc] > 0:
            labels.append(f"{WEIGHT_NAMES[wc]} ({counts[wc]})")
            sizes.append(counts[wc])
            pie_colors.append(WEIGHT_COLORS[wc])

    if not sizes:
        return

    wedges, texts, autotexts = ax.pie(sizes, labels=labels, colors=pie_colors,
                                       autopct='%1.1f%%', startangle=90,
                                       textprops={'fontsize': 8, 'color': COLORS['text']},
                                       wedgeprops={'edgecolor': COLORS['bg_dark'], 'linewidth': 1.5})
    for at in autotexts:
        at.set_fontsize(8)
        at.set_fontweight('bold')

    ax.set_title('Job Weight Distribution')


def plot_pt_boxplot(ax, data):
    """Plot 6: Per-job processing time box plot."""
    jobs = data['jobs']
    if not jobs:
        return

    # Show first 30 jobs max
    display_jobs = [j for j in jobs[:30] if j['proc_times']]
    if not display_jobs:
        return

    box_data = [j['proc_times'] for j in display_jobs]
    labels = [j['job_id'] for j in display_jobs]

    bp = ax.boxplot(box_data, patch_artist=True, notch=False,
                    medianprops={'color': COLORS['accent'], 'linewidth': 1.5},
                    whiskerprops={'color': COLORS['text'], 'linewidth': 0.8},
                    capprops={'color': COLORS['text'], 'linewidth': 0.8},
                    flierprops={'marker': 'o', 'markerfacecolor': COLORS['warning'],
                                'markersize': 3, 'alpha': 0.6})

    # Color boxes by weight class
    for i, patch in enumerate(bp['boxes']):
        wc = display_jobs[i]['weight_class']
        color = WEIGHT_COLORS.get(wc, COLORS['medium'])
        patch.set_facecolor(color)
        patch.set_alpha(0.7)
        patch.set_edgecolor(COLORS['text'])
        patch.set_linewidth(0.5)

    step = max(1, len(display_jobs) // 15)
    ax.set_xticks(range(1, len(display_jobs) + 1, step))
    ax.set_xticklabels([labels[i] for i in range(0, len(display_jobs), step)],
                       rotation=45, ha='right', fontsize=7)
    ax.set_title(f'Processing Time per Job (first {len(display_jobs)})')
    ax.set_xlabel('Job')
    ax.set_ylabel('Processing Time')
    ax.grid(True, axis='y')


def plot_machine_job_heatmap(ax, data):
    """Plot 7: Machine-Job eligibility heatmap."""
    mjm = data['machine_job_matrix']
    jobs = data['jobs']
    if not mjm or not jobs:
        return

    # Limit dimensions for readability
    max_machines = 25
    max_jobs = 30

    all_machines = sorted(data['machine_load'].keys(),
                          key=lambda x: data['machine_load'][x], reverse=True)[:max_machines]
    all_jobs = [j['job_id'] for j in jobs[:max_jobs]]

    # Build matrix: rows = machines, cols = jobs
    matrix = []
    for mid in all_machines:
        row = []
        job_set = mjm.get(mid, set())
        for jid in all_jobs:
            row.append(1 if jid in job_set else 0)
        matrix.append(row)

    if not matrix or not matrix[0]:
        return

    im = ax.imshow(matrix, aspect='auto', cmap='YlOrRd', interpolation='nearest',
                   vmin=0, vmax=1)

    ax.set_yticks(range(len(all_machines)))
    ax.set_yticklabels(all_machines, fontsize=6)

    step_x = max(1, len(all_jobs) // 12)
    ax.set_xticks(range(0, len(all_jobs), step_x))
    ax.set_xticklabels([all_jobs[i] for i in range(0, len(all_jobs), step_x)],
                       rotation=45, ha='right', fontsize=6)

    ax.set_title(f'Machine-Job Eligibility ({len(all_machines)}M x {len(all_jobs)}J)')
    ax.set_xlabel('Job')
    ax.set_ylabel('Machine')


def plot_role_comparison(ax, data):
    """Plot 8: Processing time distribution by machine role."""
    rt = data['role_times']

    box_data = []
    box_labels = []
    box_colors = []

    for role, color in [('bottleneck', COLORS['bottleneck']),
                         ('specialist', COLORS['specialist']),
                         ('normal', COLORS['normal'])]:
        if rt[role]:
            box_data.append(rt[role])
            box_labels.append(f"{role.capitalize()}\n(n={len(rt[role])})")
            box_colors.append(color)

    if not box_data:
        ax.text(0.5, 0.5, 'No role data', ha='center', va='center', transform=ax.transAxes)
        return

    bp = ax.boxplot(box_data, patch_artist=True, notch=False,
                    medianprops={'color': COLORS['text'], 'linewidth': 1.5},
                    whiskerprops={'color': COLORS['text'], 'linewidth': 0.8},
                    capprops={'color': COLORS['text'], 'linewidth': 0.8},
                    flierprops={'marker': 'o', 'markerfacecolor': COLORS['warning'],
                                'markersize': 3, 'alpha': 0.6})

    for patch, color in zip(bp['boxes'], box_colors):
        patch.set_facecolor(color)
        patch.set_alpha(0.75)
        patch.set_edgecolor(COLORS['text'])

    ax.set_xticklabels(box_labels)
    ax.set_title('Processing Time by Machine Role')
    ax.set_ylabel('Processing Time')
    ax.grid(True, axis='y')

    # Add mean markers
    for i, times in enumerate(box_data):
        mean_val = sum(times) / len(times)
        ax.scatter([i + 1], [mean_val], marker='D', color=COLORS['accent'],
                   s=30, zorder=5, label='Mean' if i == 0 else None)

    ax.legend(loc='upper right')


# ──────────────────────────────────────────────────────
#  Main dashboard assembly
# ──────────────────────────────────────────────────────

def create_dashboard(input_path, output_path=None, dpi=120, show=False):
    """Create the full 8-panel dashboard from a JSON instance file."""
    if not HAS_MPL:
        print("ERROR: matplotlib is required for visualization.")
        print("Install it:  pip install matplotlib")
        return False

    setup_style()
    data = load_instance(input_path)
    meta = data['meta']

    # Figure with 4x2 grid
    fig = plt.figure(figsize=(18, 22))
    fig.suptitle(
        f"FJSP Instance Dashboard  |  Class: {meta.get('instance_class', '?')}  |  "
        f"{meta['num_jobs']}J x {meta['num_machines']}M  |  "
        f"{meta['total_ops']} ops  |  Seed: {meta.get('seed', '?')}  |  "
        f"Dist: {meta.get('pt_distribution', '?')}",
        fontsize=13, fontweight='bold', color=COLORS['text'],
        y=0.98
    )

    gs = GridSpec(4, 2, figure=fig, hspace=0.35, wspace=0.30,
                  left=0.06, right=0.96, top=0.95, bottom=0.04)

    # Row 1
    ax1 = fig.add_subplot(gs[0, 0])
    plot_pt_histogram(ax1, data)

    ax2 = fig.add_subplot(gs[0, 1])
    plot_ops_per_job(ax2, data)

    # Row 2
    ax3 = fig.add_subplot(gs[1, 0])
    plot_flexibility_dist(ax3, data)

    ax4 = fig.add_subplot(gs[1, 1])
    plot_machine_load(ax4, data)

    # Row 3
    ax5 = fig.add_subplot(gs[2, 0])
    plot_job_weight_pie(ax5, data)

    ax6 = fig.add_subplot(gs[2, 1])
    plot_pt_boxplot(ax6, data)

    # Row 4
    ax7 = fig.add_subplot(gs[3, 0])
    plot_machine_job_heatmap(ax7, data)

    ax8 = fig.add_subplot(gs[3, 1])
    plot_role_comparison(ax8, data)

    # Save or show
    if output_path:
        os.makedirs(os.path.dirname(output_path) if os.path.dirname(output_path) else '.', exist_ok=True)
        fig.savefig(output_path, dpi=dpi, bbox_inches='tight',
                    facecolor=fig.get_facecolor(), edgecolor='none')
        print(f"Dashboard saved to: {output_path}")

    if show:
        plt.show()
    else:
        plt.close(fig)

    return True


# ──────────────────────────────────────────────────────
#  CLI
# ──────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='FJSP Instance Visualizer — Distribution Dashboard',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --input instance.json
  %(prog)s --input instance.json --output dashboard.png
  %(prog)s --input instance.json --output dashboard.png --dpi 200
"""
    )
    parser.add_argument('--input', required=True, help='Path to JSON instance file')
    parser.add_argument('--output', default=None,
                        help='Output image path (default: <input>_dashboard.png)')
    parser.add_argument('--dpi', type=int, default=120, help='Image DPI (default: 120)')
    parser.add_argument('--show', action='store_true', help='Show interactive window')

    args = parser.parse_args()

    if args.output is None:
        base = os.path.splitext(args.input)[0]
        args.output = f"{base}_dashboard.png"

    success = create_dashboard(args.input, args.output, args.dpi, args.show)
    return 0 if success else 1


if __name__ == '__main__':
    sys.exit(main())
