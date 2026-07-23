import pandas as pd
import numpy as np
import statsmodels.api as sm
import matplotlib.pyplot as plt
import seaborn as sns

df = pd.read_csv("03_data/lob_snapshots.csv")

df['datetime'] = pd.to_datetime(df['timestamp_ns'], unit='ns')
df.set_index('datetime', inplace=True)

# Compute mid-price
# Using bid_0 and ask_0 as they represent the top of the book
df['mid'] = (df['bid_0'] + df['ask_0']) / 2

# Resample to a strict 1-second grid. 
# This ensures that a .shift(5) exactly corresponds to a 5-second forward look.
print("Resampling data to 1-second intervals...")
df_resampled = df.resample('1s').last().dropna()

# Compute forward mid-price change
horizons = {'1s': 1, '5s': 5, '30s': 30, '1min': 60}

for label, periods in horizons.items():
    df_resampled[f'dMid_{label}'] = df_resampled['mid'] - df_resampled['mid'].shift(periods)

# Drop the rows at the end of the day where we cannot look forward
df_clean = df_resampled.dropna().copy()

df_clean = df_clean.between_time('09:30', '16:00')

# Run OLS & Produce Table
print("\nRunning OLS regressions...")
results_list = []

for label in horizons.keys():
    # Predictor (X): OFI_1s 
    # Target (y): Forward mid-price change at various horizons
    X = sm.add_constant(df_clean['ofi_1s'])
    y = df_clean[f'dMid_{label}']
    
    model = sm.OLS(y, X).fit()
    
    results_list.append({
        'horizon': label,
        'R^2': round(model.rsquared, 4),
        'Beta': round(model.params['ofi_1s'], 6),
        't-stat': round(model.tvalues['ofi_1s'], 2),
        'p-value': round(model.pvalues['ofi_1s'], 6)
    })

results_df = pd.DataFrame(results_list)

print("\n--- Cont 2014 Replication Summary ---")
print(results_df.to_string(index=False))
print("-------------------------------------\n")

# Plot OFI signal vs realized mid-price change (1s horizon)
plt.figure(figsize=(10, 6))

# Using seaborn's regplot to automatically handle the scatter and OLS line
sns.regplot(
    x='ofi_1s', 
    y='dMid_1s', 
    data=df_clean,
    scatter_kws={'alpha': 0.05, 's': 10, 'color': '#2c3e50'},
    line_kws={'color': '#e74c3c', 'linewidth': 2}
)

plt.title('OFI (1s) vs Forward Mid-Price Change (1s Horizon)', fontsize=14, pad=15)
plt.xlabel('Order Flow Imbalance (1s)', fontsize=12)
plt.ylabel(r'$\Delta$ Mid-Price (1s Forward)', fontsize=12)
plt.grid(True, alpha=0.3)
plt.tight_layout()

# Save the plot
plt.savefig('02_analysis/ofi_regression_plot.png', dpi=300)
plt.show()