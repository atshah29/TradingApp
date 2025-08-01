#MODEL_PATH = './models/finetuned-finbert-2'

import torch
import warnings
import tweepy 
from transformers import AutoTokenizer, AutoModelForSequenceClassification, pipeline
import requests
import datetime
import os
from dotenv import load_dotenv
import string


#include method to look for symbol AND actual stock name 

warnings.filterwarnings("ignore")


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_PATH = os.path.join(SCRIPT_DIR, "models/finetuned-finbert-2")
ENV_PATH = os.path.join(SCRIPT_DIR, "apis.env")
load_dotenv(ENV_PATH)  # Specify the path to your .env file


if not os.path.exists(MODEL_PATH):
    raise ValueError(f"Model directory not found at {MODEL_PATH}")

tokenizer = AutoTokenizer.from_pretrained(MODEL_PATH, local_files_only=True)
model = AutoModelForSequenceClassification.from_pretrained(MODEL_PATH, local_files_only=True)


bearerToken = os.getenv('bearerToken')
newsAPI = os.getenv('newsAPI')
if not bearerToken or not newsAPI:
    raise ValueError("API keys are missing. Please set them in the .env file.")


sentiment_pipeline = pipeline(
    "sentiment-analysis", 
    model=model, 
    tokenizer=tokenizer,
    device=0 if torch.cuda.is_available() else -1
)

def get_sentiment(symbol, useTwitter = False):
    """
    Get sentiment of the given symbol.
    
    Args:
        symbol (str): The symbol of the stock.
        
    Returns:
        str: Sentiment label ('positive', 'negative').
    """
    # r = requests.get('https://ticker-2e1ica8b9.now.sh/keyword/' + symbol)
    # data = r.json()
    # for element in data:
    #     if element['symbol'] == symbol:
    #         name = element['name']
    #         # Remove common suffixes
    #         name = name.rstrip(" Inc").rstrip(" Incorporated").rstrip(" Corp").rstrip(" Corporation").rstrip(" Corp.").rstrip(" Ltd").rstrip(" Limited").strip()
    #         break
    # translator = str.maketrans('','', string.punctuation)
    # name = name.translate(translator)  # Remove punctuation
    # print(name)
    

    allSentiments = []
    if useTwitter:
        Client = tweepy.Client(bearer_token=bearerToken, wait_on_rate_limit=True)
        query = f"{symbol} -is:retweet lang:en"
        response = Client.search_recent_tweets(query = query)
        for tweet in response.data:
            sentiment = sentiment_pipeline(tweet.text)
            allSentiments.append(sentiment[0]['label'])


    today = datetime.datetime.today().date()
    two_week_ago = today - datetime.timedelta(weeks=2)
    today.strftime("%Y-%m-%d")
    two_week_ago.strftime("%Y-%m-%d")
    url = f"https://newsapi.org/v2/everything?q={symbol}&searchIn=title&sortyBy=popularity&from={two_week_ago}&to={today}&language=en&apiKey={newsAPI}"
    response = requests.get(url)
    response = response.json()
    for article in response['articles']:
        sentiment = sentiment_pipeline(article['title'])
        if( sentiment[0]['score'] < 0.9):
            continue
        #print(f"Title: {article['title']}, Sentiment: {sentiment[0]['label']}")
        allSentiments.append(sentiment[0]['label'])
    
    # print(f"Positive count : {allSentiments.count('POSITIVE')}, Negative count : {allSentiments.count('NEGATIVE')}")
    if allSentiments.count('POSITIVE') > allSentiments.count('NEGATIVE'):
        return 'Positive'
    elif allSentiments.count('NEGATIVE') > allSentiments.count('POSITIVE'):
        return 'Negative'
    else:  
        return 'Neutral'

#print(get_sentiment('PLTR', 0))  # Example usage

if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        symbol = sys.argv[1]
        use_twitter = len(sys.argv) > 2 and sys.argv[2] == '1'
        print(f"The sentiment for {symbol} is: {get_sentiment(symbol, use_twitter)}")
    else:
        print("Please provide a stock symbol as an argument.")





