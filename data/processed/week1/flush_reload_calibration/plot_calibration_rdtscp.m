%% plot_calibration_rdtscp.m
% MATLAB R2022a
% Plot an RDTSCP hit-vs-miss latency histogram in a style similar to
% Figure 5 of Yarom and Falkner's FLUSH+RELOAD paper.
%
% Place this script in the same folder as:
%   calibration_rdtscp_50000.csv
%
% Required CSV columns:
%   index, hit_ticks, miss_ticks

clear;
clc;
close all;

%% 1. Locate and read the raw-data file
scriptPath = fileparts(mfilename('fullpath'));
if isempty(scriptPath)
    scriptPath = pwd;
end

csvFile = fullfile(scriptPath, 'calibration_rdtscp_50000.csv');
if ~isfile(csvFile)
    error('Cannot find the input file: %s', csvFile);
end

T = readtable(csvFile);
requiredColumns = {'hit_ticks', 'miss_ticks'};
if ~all(ismember(requiredColumns, T.Properties.VariableNames))
    error('The CSV must contain columns named hit_ticks and miss_ticks.');
end

hitTicks  = double(T.hit_ticks);
missTicks = double(T.miss_ticks);
hitTicks  = hitTicks(isfinite(hitTicks));
missTicks = missTicks(isfinite(missTicks));

if isempty(hitTicks) || isempty(missTicks)
    error('The hit_ticks or miss_ticks column contains no valid samples.');
end

if height(T) < 1000
    warning(['This CSV contains only %d rows. A Week-1 calibration should normally ' ...
             'contain thousands of samples.'], height(T));
end

%% 2. Calibration threshold
% The main RDTSCP hit cluster is around 98-102 ticks. Rare hit samples reach
% 156 ticks, while the smallest observed memory miss is 824 ticks. A
% threshold of 170 ticks keeps all observed hits below the threshold and is
% still far from the memory-miss distribution.
thresholdTicks = 170;

falseMissRate = mean(hitTicks >= thresholdTicks);  % cache hit classified as miss
falseHitRate  = mean(missTicks < thresholdTicks);  % memory miss classified as hit

fprintf('\nRDTSCP calibration summary\n');
fprintf('--------------------------\n');
fprintf('Hit samples       : %d\n', numel(hitTicks));
fprintf('Miss samples      : %d\n', numel(missTicks));
fprintf('Hit median        : %.1f TSC ticks\n', median(hitTicks));
fprintf('Hit range         : %.1f to %.1f TSC ticks\n', min(hitTicks), max(hitTicks));
fprintf('Miss median       : %.1f TSC ticks\n', median(missTicks));
fprintf('Miss range        : %.1f to %.1f TSC ticks\n', min(missTicks), max(missTicks));
fprintf('Selected threshold: %.1f TSC ticks\n', thresholdTicks);
fprintf('False-miss rate   : %.6f%%\n', 100 * falseMissRate);
fprintf('False-hit rate    : %.6f%%\n\n', 100 * falseHitRate);

%% 3. Build probability histograms
% A 2-tick bin width preserves the discrete timing structure of the raw data.
binWidth = 2;
allTicks = [hitTicks; missTicks];
xMax = ceil(max(allTicks) / 100) * 100;
if xMax < 200
    xMax = 200;
end

binEdges   = -1:binWidth:(xMax + 1);
binCenters = binEdges(1:end-1) + binWidth / 2;

hitProbability  = histcounts(hitTicks,  binEdges, 'Normalization', 'probability');
missProbability = histcounts(missTicks, binEdges, 'Normalization', 'probability');

%% 4. Draw the Figure-5-style scientific plot
fig = figure('Color', 'w', 'Units', 'pixels', 'Position', [100 100 1200 500]);
ax = axes(fig);
hold(ax, 'on');

% Paper-like colors: memory in blue and L1 cache in green.
bar(ax, binCenters, missProbability, 1.0, ...
    'FaceColor', [0.00 0.15 0.90], ...
    'EdgeColor', 'none', ...
    'DisplayName', 'From Memory');

bar(ax, binCenters, hitProbability, 1.0, ...
    'FaceColor', [0.00 0.85 0.00], ...
    'EdgeColor', 'none', ...
    'DisplayName', 'From L1 Cache');

thresholdLine = xline(ax, thresholdTicks, '--k', ...
    sprintf('Threshold = %d ticks', thresholdTicks), ...
    'LineWidth', 1.2, ...
    'LabelVerticalAlignment', 'middle', ...
    'LabelHorizontalAlignment', 'left');
thresholdLine.HandleVisibility = 'off';

xlim(ax, [0 xMax]);
ylim(ax, [0 1]);
xticks(ax, 0:200:xMax);
yticks(ax, 0:0.1:1);
ytickformat(ax, 'percentage');

xlabel(ax, 'Probe Time (TSC ticks)');
ylabel(ax, 'Fraction of Samples');
title(ax, 'RDTSCP Calibration: Hit-vs-Miss Load-Time Distribution');

legend(ax, 'Location', 'northeast', 'Box', 'off');
box(ax, 'on');
grid(ax, 'off');
set(ax, ...
    'FontName', 'Times New Roman', ...
    'FontSize', 11, ...
    'LineWidth', 1.0, ...
    'TickDir', 'out', ...
    'Layer', 'top');

%% 5. Export publication-quality files
pngFile = fullfile(scriptPath, 'calibration_rdtscp_histogram.png');
pdfFile = fullfile(scriptPath, 'calibration_rdtscp_histogram.pdf');
figFile = fullfile(scriptPath, 'calibration_rdtscp_histogram.fig');

exportgraphics(fig, pngFile, 'Resolution', 300);
exportgraphics(fig, pdfFile, 'ContentType', 'vector');
savefig(fig, figFile);

fprintf('Saved plot files:\n  %s\n  %s\n  %s\n', pngFile, pdfFile, figFile);
