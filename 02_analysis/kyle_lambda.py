import pandas as pd
import numpy as np
import statsmodels.api as sm
import matplotlib.pyplot as plt
from matplotlib.dates import DateFormatter

def calculate_kyles_lambda(trades_csv, quotes_csv):
    print("Loading data...")

    # Load Data
    trades = pd.read_csv(trades_csv, parse_dates=['timestamp']).sort_values('timestamp')
    quotes = pd.read_csv(quotes_csv, parse_dates=['timestamp']).sort_values('timestamp')

    # Calculate mid-price for quotes
    quotes['mid'] = (quotes['bid'] + quotes['ask']) / 2.0

    print("Aligning trades with prevailing mid-price...")

    # Merge prevailing quote to each trade
    df = pd.merge_asof(trades, quotes[['timestamp', 'mid']], 
                       on='timestamp', direction='backward')

    # Drop trades that happened before the first quote
    df.dropna(subset=['mid'], inplace=True)

    print("Applying Lee-Ready Rule...")

    # Lee-Ready Rule: Classify Trade Direction
    df['direction'] = 0
    df.loc[df['price'] > df['mid'], 'direction'] = 1  # Buyer-initiated
    df.loc[df['price'] < df['mid'], 'direction'] = -1 # Seller-initiated
    
    # Handle trades exactly at the mid-price (Zero-Tick Rule)
    # Replace 0s with NaN, forward-fill the previous direction, and default to 1 if it's the first trade
    df['direction'] = df['direction'].replace(0, np.nan).ffill().fillna(1)

    # Compute Signed Order Flow (OF)
    df['OF'] = df['direction'] * df['size']

    # Compute price change (Delta Mid) 
    df['dMid'] = df['mid'].diff().shift(-1).fillna(0) # Change to next prevailing mid

    # Set timestamp as index for time-based rolling windows
    df.set_index('timestamp', inplace=True)
    
    # Filter for standard trading hours (9:30 to 16:00)
    df = df.between_time('09:30', '16:00')

    print("Estimating Kyle's Lambda (30-min rolling OLS)...")
    # Rolling 30-minute OLS (ΔMid ~ OF)
    # Since trade frequency is irregular, resample to 1-minute bins to stabilize the regression, 
    # summing the Order Flow and taking the cumulative price change.
    resampled = df.resample('1min').agg({
        'OF': 'sum',
        'dMid': 'sum'
    }).dropna()

    # Use Statsmodels RollingOLS
    # 30 periods of 1-minute bins = 30-minute rolling window
    window_size = 30 
    
    # Add constant for the intercept (Epsilon)
    exog = sm.add_constant(resampled['OF'])
    endog = resampled['dMid']
    
    from statsmodels.regression.rolling import RollingOLS
    model = RollingOLS(endog, exog, window=window_size)
    rolling_res = model.fit()
    
    # Extract the coefficient for OF (this is Kyle's Lambda)
    resampled['lambda'] = rolling_res.params['OF']

    # Plotting the Intraday Pattern
    print("Generating Plot...")
    plot_intraday_lambda(resampled)

def plot_intraday_lambda(df):
    # Drop the NaN values from the initial 30-min window ramp-up
    plot_data = df.dropna(subset=['lambda'])
    
    # Smooth the line slightly for visualization
    plot_data['lambda_smooth'] = plot_data['lambda'].rolling(window=5, min_periods=1).mean()

    plt.figure(figsize=(12, 6))
    
    # Plot raw lambda and smoothed lambda
    plt.plot(plot_data.index, plot_data['lambda'], alpha=0.3, color='#3498db', label="30-min Lambda")
    plt.plot(plot_data.index, plot_data['lambda_smooth'], linewidth=2.5, color='#2c3e50', label="Smoothed Trend")

    plt.title("Intraday Kyle's Lambda ($\lambda$) - Market Impact Over Time", fontsize=16, pad=15)
    plt.xlabel("Time of Day", fontsize=12)
    plt.ylabel("$\lambda$ (Price Impact per unit of Order Flow)", fontsize=12)
    
    # Format x-axis to show HH:MM nicely
    ax = plt.gca()
    ax.xaxis.set_major_formatter(DateFormatter('%H:%M'))
    
    plt.grid(True, alpha=0.3, linestyle='--')
    plt.legend(loc='upper right', frameon=True)
    plt.tight_layout()
    
    plt.savefig('kyle_lambda_intraday.png', dpi=300)
    print("Saved plot to 'kyle_lambda_intraday.png'")
    # plt.show() # Uncomment if running interactively

if __name__ == "__main__":
    calculate_kyles_lambda("trades_output.csv", "quotes_output.csv")
    pass