import { Component, OnInit, OnDestroy } from '@angular/core';

@Component({
  selector: 'app-mode',
  imports: [],
  templateUrl: './mode.html',
  styleUrl: './mode.scss'
})
export class Mode implements OnInit, OnDestroy {
  status: string = '';
  private intervalId: any;

  ngOnInit() {
    this.fetchMode();
    this.intervalId = setInterval(() => this.fetchMode(), 5000);
  }

  ngOnDestroy() {
    if (this.intervalId) {
      clearInterval(this.intervalId);
    }
  }

  fetchMode() {
    fetch('/mode')
      .then(res => {
        if (!res.ok) {
          throw new Error('Network response was not ok');
        }
        return res.text();
      })
      .then((data: string) => {
        const mode = parseInt(data, 10);
        this.status = this.getStatus(mode);
      })
      .catch((error) => {
        this.status = 'Error fetching mode';
        console.error(error);
      });
  }

  getStatus(mode: number): string {
    switch (mode) {
      case 0: return '💤';
      case 1: return '👀';
      case 2: return '🩸';
      default: return '💀';
    }
  }
}
