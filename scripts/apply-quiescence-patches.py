#!/usr/bin/env python3
"""Apply the canonical runtime series and optional renderer quiescence patches."""
from dependency_patches import main

if __name__ == '__main__':
    main(('runtime', 'rt64', 'frontend'))
