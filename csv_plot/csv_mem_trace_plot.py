#!/usr/bin/python3
"""csv_mem_trace_plot this script is designed to parse and plot the
trace files produced by the our memory benchmark. The trace files
are expected to have the following format:

    line 0 (plan size): plan_size
    lines 1 (headers): op_type,chunk_size,block_id,exec_time
    lines 2...n (actual traces): malloc,36757504,0,8580.000000

Usage:
    csv_mem_trace_plot.py [(--tlsf=TLSF_FILE) (--tlsf_ori=TLSF_ORI_FILE) \
    (--native=NATILE_FILE) (-f FVAL | --free_cutoff=FVAL) (-m MVAL | --malloc_cutoff=MVAL) \
    (-o DIR | --out_dir=DIR) (-c | --console)]
    csv_mem_trace_plot.py (-i FILE | --infile=FILE)
    csv_mem_trace_plot.py (-h | --help)
    csv_mem_trace_plot.py (-v | --version)

Arguments:
    -h --help                       show this message
    --tlsf=TLSF_FILE                import a tlsf trace file
    --tlsf_ori=TLSF_ORI_FILE        import a tlsf original trace file
    --native=NATIVE_FILE            import a native trace file
    -f FVAL --free_cutoff=FVAL      free op cutoff to count as "over budget" [default: 800].
    -m MVAL --malloc_cutoff=MVA L   malloc op cutoff to count as "over budget" [default: 2000].
    -o DIR --out_dir=DIR            location to save the generated plots [default: ./traces/].
    -v --version                    show version number.
    -c --console                    enable if executing from console to avoid errors [default: False].
"""
import numpy as np
import csv
import matplotlib
from docopt import docopt
from math import ceil
from os import remove
from os.path import basename, isfile

free_cutoff = 800
malloc_cutoff = 2000
out_dir = None
plt = None
# infile = "./traces/20180629T195418Z_mem_trace_out.csv"

# 3.4 gb
# infile2 = "./traces/20180808T055857Z_tlsf_mem_trace_out.csv"
# infile = "./traces/20180808T055659Z_native_mem_trace_out.csv"

# 300 mb
infile = "./traces/20180808T021350Z_native_mem_trace_out.csv"
infile2 = "./traces/20180808T021400Z_tlsf_mem_trace_out.csv"

tlsf_infile = None
tlsf_ori_infile = None
native_infile = None
fcnt = 0


def parse_file(f):
    """parse the csv file that contains the traces of the executed plan

    :param f: file path of the trace file
    :return: nothing
    """
    fname_base = basename(f)
    print(" -- Parsing filename: {}".format(fname_base))
    global out_dir
    with open(f, 'r') as csv_fp:
        csv_reader = csv.reader(csv_fp)
        # skip the first two lines
        next(csv_reader)
        next(csv_reader)
        malloc_spikes = 0
        free_spikes = 0
        # parse rows
        malloc_len = list()
        free_len = list()
        malloc_chunk_len = list()
        free_chunk_len = list()
        # loop through the file and parse its lines
        for l in csv_reader:
            c_tim = int(float(l[3]))
            c_chunk = int(l[1]) / (1024 * 1024)
            # append to the joined list
            # append to the individual list
            if l[0] == "malloc":
                malloc_len.append(c_tim)
                malloc_chunk_len.append(c_chunk)
                if c_tim > malloc_cutoff:
                    malloc_spikes += 1
            else:
                free_len.append(c_tim)
                free_chunk_len.append(c_chunk)
                if c_tim > free_cutoff:
                    free_spikes += 1
        plot_hist2d(fname_base + "_malloc", out_dir, "malloc ops", malloc_len, malloc_chunk_len, 40)
        plot_hist2d(fname_base + "_free", out_dir, "free ops", free_len, free_chunk_len, 40)
        plot_hist_comb(fname_base, out_dir, malloc_len, free_len, "malloc", "free", 20)
        #plot_hist_comb_log(fname_base, out_dir, malloc_len, "malloc", free_len, "free", title_tag="native", bins=40)
        total_ops = len(malloc_len) + len(free_len)
        print(" ** Trace aggregate statistics")
        print(" -- Parsed filename: {}".format(fname_base))
        print(" -- Cutoff threshold (in cycles): {} cycles for free and {} cycles for malloc"
              .format(free_cutoff, malloc_cutoff))
        print("\t --- Total spikes: {} out of {} ops".format(malloc_spikes + free_spikes, total_ops))
        print("\t --- Total malloc spikes: {} out of {} ops".format(malloc_spikes, len(malloc_len)))
        print("\t --- Total free spikes: {} out of {} ops".format(free_spikes, len(free_len)))
        print("\t --- Total spike %: {} %".format((100.0 * (malloc_spikes + free_spikes)) / total_ops))
        print("\t --- Total malloc spike %: {} %".format((100.0 * malloc_spikes) / len(malloc_len)))
        print("\t --- Total free spike %: {} %".format((100.0 * free_spikes) / len(free_len)))

        return malloc_len, malloc_chunk_len, free_len, free_chunk_len, fname_base


def plot_hist2d(fname, dir_path, title_tag, data_a, data_b, bins):
    """This function plots two variables that have the same size in a 2d histogram

    :param fname: filename, which is used for the output filename
    :param dir_path: the output directory path
    :param title_tag: title for the graph
    :param data_a: data a
    :param data_b: data b
    :param bins: the number of bins to use
    :return:
    """
    max_value_a = int(np.mean(data_a)) * 3
    max_value_b = int(np.mean(data_b)) * 3

    bin_step_a = max_value_a / bins
    bin_step_b = max_value_b / bins

    binsa = np.arange(0, max_value_a, bin_step_a)
    binsb = np.arange(0, max_value_b, bin_step_b)

    hist2d_fig = plt.figure()
    plt.hist2d(np.clip(data_a, binsa[0], binsa[-1]),
               np.clip(data_b, binsb[0], binsb[-1]), bins=bins)
    plt.colorbar()
    plt.ylabel("Block size (MB)")
    plt.xlabel("Cycles spent")
    plt.title(str(len(data_b)) + " " + title_tag)
    plt.show()
    fpath = dir_path + "/" + fname + "_chunk_vs_cycles" + ".pdf"
    if isfile(fpath):
        remove(fpath)
    print(" ** Info saving: {} to path: {}".format(fname, dir_path))
    hist2d_fig.savefig(fpath, bbox_inches='tight')


def plot_hist_comb(fname, dir_path, data_a, data_b, data_a_tag, data_b_tag, bin_no):
    """This function plots two histograms in the same figure using the same
    bin thresholds for both

    :param fname: filename, which is used for the output filename
    :param dir_path: the output directory path
    :param data_a: data a
    :param data_b: data b
    :param bin_no: the number of bins to use
    :return: nothing
    """
    # find the reference for the bin clipping points
    max_value_a = int(np.mean(data_a)) * 2
    max_value_b = int(np.mean(data_b)) * 2

    # take the largest value as a reference
    if max_value_a > max_value_b:
        max_val = max_value_a
    else:
        max_val = max_value_b

    # now round up the value to the nearest number divisible by 100
    max_val = roundup(max_val, 100)
    # do the same for the bins
    bin_step = roundup(max_val / bin_no, 100)
    bins = np.arange(0, max_val, bin_step)

    hist_fig, ax = plt.subplots(figsize=(18, 5))
    _, bins, patches = plt.hist([np.clip(data_a, bins[0], bins[-1]),
                                 np.clip(data_b, bins[0], bins[-1])],
                                density=True,
                                bins=bins, color=['#0087ab', '#a7cbeb'],
                                label=[data_a_tag, data_b_tag])

    xlabels = [str(b) for b in bins[1:]]
    xlabels[-1] += '+'

    num_of_labels = len(xlabels)
    plt.xlim([0, max_val])
    plt.xticks(bin_step * np.arange(num_of_labels) + bin_step / 2)
    ax.set_xticklabels(xlabels)
    plt.xlabel("cycles spent")

    plt.yticks([])
    plt.ylabel("density")
    plt.title("cpu cycles for total ops: " + str(len(data_a) + len(data_b)))
    plt.setp(patches, linewidth=0)
    plt.legend(loc='upper right')

    hist_fig.tight_layout()
    plt.show()
    fpath = dir_path + "/" + fname + ".pdf"
    if isfile(fpath):
        remove(fpath)
    print(" ** Info saving: {} to path: {}".format(fname, dir_path))
    hist_fig.savefig(fpath, bbox_inches='tight')


def plot_hist_comb_log(fname, dir_path, data_a, a_label, data_b, b_label, bins, title_tag=None, onlylog=True):
    """
    Plots a combined histogram of the two datasets using both normal and logscale
    :param data_a:
    :param data_b:
    :param bins:
    :return:
    """
    data_a_min = np.min(data_a)
    data_a_max = np.max(data_a)
    data_b_min = np.min(data_b)
    data_b_max = np.max(data_b)
    print(" ** Data a ({}) min {}, max {}\n ** Data b ({}) min {}, max {}\n"
          .format(a_label, data_a_min, data_a_max, b_label, data_b_min, data_b_max))
    hist_fig = plt.figure(figsize=(18, 12))
    _ = hist_fig.add_subplot(211)
    linhist_a, linbins_a, _ = plt.hist(data_a, bins=bins, alpha=0.5, label=a_label)
    linhist_b, linbins_b, _ = plt.hist(data_b, bins=bins, alpha=0.5, label=b_label)
    if title_tag is not None:
        plt.title("{} {} ops latency in cycles".format(len(data_a), title_tag))
    else:
        plt.title("{} ops latency in cycles".format(len(data_a)))
    plt.xlabel("cycles spent")
    plt.ylabel("op count")
    plt.legend(loc='upper right')
    logbins_a = np.logspace(np.log10(linbins_a[0]),
                            np.log10(linbins_a[-1]), len(linbins_a))
    logbins_b = np.logspace(np.log10(linbins_b[0]),
                            np.log10(linbins_b[-1]), len(linbins_b))
    if onlylog:
        hist_fig.clf()
        _ = hist_fig.add_subplot(111)
    else:
        _ = hist_fig.add_subplot(212)

    n_a, bin_edges_a, _ = plt.hist(data_a, bins=logbins_a, alpha=0.5, label=a_label, align='mid')
    n_b, bin_edges_b, _ = plt.hist(data_b, bins=logbins_b, alpha=0.5, label=b_label, align='mid')
    if title_tag is not None:
        plt.title("{} {} ops latency in cycles [log scale]".format(len(data_a), title_tag))
    else:
        plt.title("{} ops latency in cycles [log scale]".format(len(data_a)))
    plt.xlabel("cycles spent (log scale)")
    plt.ylabel("op count")
    plt.xscale('log')
    plt.legend(loc='upper right')

    centers_a = 0.5 * (bin_edges_a[1:] + bin_edges_a[:-1])
    centers_b = 0.5 * (bin_edges_b[1:] + bin_edges_b[:-1])
    plt.errorbar(centers_a, n_a, fmt="o", ms=8.0)
    plt.errorbar(centers_b, n_b, fmt="^", ms=8.0)

    plt.show()

    fpath = dir_path + "/" + fname + "_hist_plot.pdf"
    if isfile(fpath):
        remove(fpath)
    print(" ** Info: saving: {} to path: {}\n".format(fname, dir_path))
    hist_fig.savefig(fpath, bbox_inches='tight')


def plot_hist_triple_log(fname, dir_path, data_a, a_label, data_b, b_label, data_c, c_label,
                         bins, title_tag=None, onlylog=True):
    """

    :param fname:
    :param dir_path:
    :param data_a:
    :param a_label:
    :param data_b:
    :param b_label:
    :param data_c:
    :param c_label:
    :param bins:
    :param title_tag:
    :param onlylog:
    :return:
    """
    data_a_min = np.min(data_a)
    data_a_max = np.max(data_a)
    data_b_min = np.min(data_b)
    data_b_max = np.max(data_b)
    data_c_min = np.min(data_c)
    data_c_max = np.max(data_c)
    print(" ** Data a ({}) min {}, max {}\n ** Data b ({}) min {}, max {}\n ** Data c ({}): min {}, max {}\n"
          .format(a_label, data_a_min, data_a_max, b_label, data_b_min, data_b_max, c_label, data_c_min, data_c_max))
    hist_fig = plt.figure(figsize=(18, 12))
    _ = hist_fig.add_subplot(211)
    linhist_a, linbins_a, _ = plt.hist(data_a, bins=bins, alpha=0.6, label=a_label)
    linhist_b, linbins_b, _ = plt.hist(data_b, bins=bins, alpha=0.5, label=b_label)
    linhist_c, linbins_c, _ = plt.hist(data_c, bins=bins, alpha=0.4, label=c_label)
    if title_tag is not None:
        plt.title("{} {} ops latency in cycles".format(len(data_a), title_tag))
    else:
        plt.title("{} ops latency in cycles".format(len(data_a)))
    plt.xlabel("cycles spent")
    plt.ylabel("op count")
    plt.legend(loc='upper right')
    logbins_a = np.logspace(np.log10(linbins_a[0]),
                            np.log10(linbins_a[-1]), len(linbins_a))
    logbins_b = np.logspace(np.log10(linbins_b[0]),
                            np.log10(linbins_b[-1]), len(linbins_b))
    logbins_c = np.logspace(np.log10(linbins_c[0]),
                            np.log10(linbins_c[-1]), len(linbins_c))
    if onlylog:
        hist_fig.clf()
        _ = hist_fig.add_subplot(111)
    else:
        _ = hist_fig.add_subplot(212)

    n_a, bin_edges_a, _ = plt.hist(data_a, bins=logbins_a, alpha=0.6, label=a_label, align='mid')
    n_b, bin_edges_b, _ = plt.hist(data_b, bins=logbins_b, alpha=0.5, label=b_label, align='mid')
    n_c, bin_edges_c, _ = plt.hist(data_c, bins=logbins_c, alpha=0.4, label=c_label, align='mid')
    if title_tag is not None:
        plt.title("{} {} ops latency in cycles [log scale]".format(len(data_a), title_tag))
    else:
        plt.title("{} ops latency in cycles [log scale]".format(len(data_a)))
    plt.xlabel("cycles spent (log scale)")
    plt.ylabel("op count")
    plt.xscale('log')
    plt.legend(loc='upper right')

    centers_a = 0.5 * (bin_edges_a[1:] + bin_edges_a[:-1])
    centers_b = 0.5 * (bin_edges_b[1:] + bin_edges_b[:-1])
    centers_c = 0.5 * (bin_edges_c[1:] + bin_edges_c[:-1])
    plt.errorbar(centers_a, n_a, fmt="o", ms=8.0)
    plt.errorbar(centers_b, n_b, fmt="^", ms=8.0)
    plt.errorbar(centers_c, n_c, fmt="*", ms=8.0)

    plt.show()

    fpath = dir_path + "/" + fname + "_hist_plot.pdf"
    if isfile(fpath):
        remove(fpath)
    print(" ** Info: saving: {} to path: {}\n".format(fname, dir_path))
    hist_fig.savefig(fpath, bbox_inches='tight')


def roundup(val, base):
    """
    Rounds the value to the nearest value that is a multiple of base.
    :param val: the value to be rounded
    :param base: the base to round to
    :return: the final rounded value
    """
    return int(ceil(val / base) * base)


def parse_args():
    """
    This function parses the docopt dictionary and sets the respective globals
    to their parsed or default values.
    :return: nothing
    """
    # scope in the variables
    global free_cutoff
    global malloc_cutoff
    global out_dir
    global infile
    global tlsf_infile
    global tlsf_ori_infile
    global native_infile
    global plt
    global fcnt
    # parse and generate the docopt dictionary
    arg_dict = docopt(__doc__, version="0.1.0")
    # print(arg_dict)

    # handle malloc op cutoff
    parsed_tag = tag_digit(arg_dict, "--malloc_cutoff")
    if parsed_tag == 0:
        print(" !! Error: Invalid value given for malloc cutoff, using default ({})"
              .format(malloc_cutoff))
    else:
        print(" ** Info: malloc op cutoff is: {}"
              .format(parsed_tag))
        malloc_cutoff = parsed_tag

    # handle free op cutoff
    parsed_tag = tag_digit(arg_dict, "--free_cutoff")
    if parsed_tag == 0:
        print(" !! Error: Invalid value given for free cutoff, using default ({})"
              .format(free_cutoff))
    else:
        print(" ** Info: free op cutoff is: {}"
              .format(parsed_tag))
        free_cutoff = parsed_tag

    # handle console flag
    if arg_dict["--console"] is not False:
        matplotlib.use('agg')

    # now import the plt
    from matplotlib import pyplot
    plt = pyplot

    # handle output directory
    if arg_dict["--out_dir"] is not None:
        out_dir = arg_dict["--out_dir"]
        if not out_dir.endswith("/"):
            out_dir = out_dir + "/"
        print(" ** Info: using output directory: {}".format(out_dir))

    # handle input files
    if arg_dict["--tlsf"] is not None:
        tlsf_infile = arg_dict["--tlsf"]
        fcnt += 1
        print(" ** Info: using input tlsf trace file: {}".format(tlsf_infile))

    if arg_dict["--tlsf_ori"] is not None:
        tlsf_ori_infile = arg_dict["--tlsf_ori"]
        fcnt += 1
        print(" ** Info: using input tlsf original trace file: {}".format(tlsf_ori_infile))

    if arg_dict["--native"] is not None:
        fcnt += 1
        native_infile = arg_dict["--native"]
        print(" ** Info: using input native allocator trace file: {}".format(native_infile))


def tag_digit(docopt_dict, tag):
    """
    This function is responsible for parsing an argument from the docopt dictionary
    while checking if it's a number or not.
    :param docopt_dict: the docopt parsed dictionary
    :param tag: the argument tag to check the value
    :return: the value if set, zero (0) otherwise
    """
    if docopt_dict[tag] is not None and docopt_dict[tag].isdigit():
        return int(docopt_dict[tag])
    else:
        return 0


#def figure_gen_hist(fname, dir_path, data_a, a_label, data_b, b_label, bins, title_tag=None):
#    plot_hist_comb_log(fname, dir_path, data_a, a_label, data_b, b_label, bins, title_tag=title_tag)


def parse_and_plot_two(infile_a, tag_a, infile_b, tag_b, same_run=False):
    """
    This function is responsible for parsing and plotting two trace files

    :param same_run:
    :param infile_a:
    :param tag_a:
    :param infile_b:
    :param tag_b:
    :return:
    """

    # check if files are from the same run
    tok1 = basename(infile_a).split("_")[0]
    tok2 = basename(infile_b).split("_")[0]
    if same_run and tok1 != tok2:
        # check if the strings
        print(" ** Trace ISO8086 timestamp for {} is: {}".format(tag_a, tok1))
        print(" ** Trace ISO8086 timestamp for {} is: {}".format(tag_b, tok2))
        print(" !! Provided traces ISO8086 timestamps seem to mismatch, if different run set same_run to False")
        return

    # parse the files
    malloc_len_a, malloc_chunk_len_a, free_len_a, free_chunk_len_a, fname_base_a = parse_file(infile_a)

    malloc_len_b, malloc_chunk_len_b, free_len_b, free_chunk_len_b, fname_base_b = parse_file(infile_b)

    plot_hist_comb_log("{}_{}_vs_{}_malloc_op".format(tok1, tag_a, tag_b), out_dir, malloc_len_a, tag_a, malloc_len_b,
                       tag_b, 40, title_tag="malloc")

    plot_hist_comb_log("{}_{}_vs_{}_free_op".format(tok1, tag_a, tag_b), out_dir, free_len_a, tag_a, free_len_b,
                       tag_b, 40, title_tag="free")


def handle_single_trace():
    if tlsf_infile is not None:
        print(" ** Single trace: tlsf pool")
    if tlsf_ori_infile is not None:
        print(" ** Single trace: tlsf original pool")
    else:
        print(" ** Single trace: native allocator")
    print("one")


def handle_two_traces():
    """
    Handle the plotting of two trace file simultaneously.

    :return: nothing
    """
    if tlsf_infile is not None:
        if tlsf_ori_infile is not None:
            print(" ** Two traces: tlsf & tlsf ori allocators")
            parse_and_plot_two(tlsf_infile, "tlsf", tlsf_ori_infile, "tlsf_ori")
        else:
            print(" ** Two traces: tlsf & native allocators")
            parse_and_plot_two(tlsf_infile, "tlsf", native_infile, "native")
    elif tlsf_ori_infile is not None:
        print(" ** Two traces: tlsf ori & native allocators")
        parse_and_plot_two(tlsf_ori_infile, "tlsf_ori", native_infile, "native")


def handle_three_traces(same_run=True):
    print(" ** Three traces: tlsf, tlsf_ori, and native allocators")

    tok1 = basename(tlsf_infile).split("_")[0]
    tok2 = basename(tlsf_ori_infile).split("_")[0]
    tok3 = basename(native_infile).split("_")[0]
    # check if files are from the same run
    if same_run:
        # check if the strings
        if tok1 != tok2 or tok1 != tok3:
            print(" ** Trace ISO8086 timestamp for tlsf is: {}".format(tok1))
            print(" ** Trace ISO8086 timestamp for tlsf original is: {}".format(tok2))
            print(" ** Trace ISO8086 timestamp for native is: {}".format(tok3))
            print(" !! Provided traces ISO8086 timestamps seem to mismatch, if different run set same_run to False")
            return


    # parse the files
    malloc_len_a, malloc_chunk_len_a, free_len_a, free_chunk_len_a, fname_base_a = parse_file(tlsf_infile)
    malloc_len_b, malloc_chunk_len_b, free_len_b, free_chunk_len_b, fname_base_b = parse_file(tlsf_ori_infile)
    malloc_len_c, malloc_chunk_len_c, free_len_c, free_chunk_len_c, fname_base_c = parse_file(native_infile)
    # plot the histograms
    fname = "{}_native_vs_tlsf_vs_tlsf_ori_malloc_op".format(tok1)
    plot_hist_triple_log(fname, out_dir, malloc_len_a, "tlsf", malloc_len_b, "tlsf_ori",
                         malloc_len_c, "native", 40, title_tag="malloc", onlylog=True)
    fname = "{}_native_vs_tlsf_vs_tlsf_ori_free_op".format(tok1)
    plot_hist_triple_log(fname, out_dir, free_len_a, "tlsf", free_len_b, "tlsf_ori",
                         free_len_c, "native", 40, title_tag="free", onlylog=True)



if __name__ == '__main__':
    """
    main stub, mainly parses arguments and calls the file parse method
    """
    parse_args()
    # decide what to do
    if fcnt == 1:
        handle_single_trace()
    elif fcnt == 2:
        handle_two_traces()
    elif fcnt == 3:
        handle_three_traces()
    else:
        print(" !! Error: unknown state or no files provided\n")