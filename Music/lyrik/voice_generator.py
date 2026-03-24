import argparse
import scipy.io.wavfile
import os
import numpy as np

os.environ["SUNO_USE_SMALL_MODELS"] = "True"
from bark import SAMPLE_RATE, generate_audio, preload_models

parser = argparse.ArgumentParser()
parser.add_argument("--text", type=str, required=True)
parser.add_argument("--style", type=str, default="")
parser.add_argument("--voice", type=str, default="") # NEU
parser.add_argument("--output", type=str, default="output_voice.wav")
args = parser.parse_args()

print(f"[BASTION] Lade Modelle für Speaker: {args.voice if args.voice else 'Random'}")
preload_models()

# Prompt bauen
full_prompt = f"{args.style} {args.text}".strip()
voice_preset = args.voice if args.voice != "" else None

# Generieren mit fixem Sprecher (Preset)
audio_array = generate_audio(
    full_prompt, 
    history_prompt=voice_preset, # Hier wird die Stimme festgesetzt!
    text_temp=0.75, 
    waveform_temp=0.75
)

# Normalisierung gegen Clipping
max_val = np.max(np.abs(audio_array))
if max_val > 0:
    audio_array = (audio_array / max_val) * 0.75

scipy.io.wavfile.write(args.output, SAMPLE_RATE, audio_array)
print(f"[SUCCESS] Vocals erstellt mit {args.voice}")