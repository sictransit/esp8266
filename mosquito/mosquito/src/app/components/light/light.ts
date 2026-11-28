import { N } from '@angular/cdk/keycodes';
import { Component, OnInit, OnDestroy } from '@angular/core';

@Component({
  selector: 'app-light',
  imports: [],
  templateUrl: './light.html',
  styleUrl: './light.scss'
})
export class Light implements OnInit, OnDestroy {
  lightLevel: number = NaN;
  private intervalId: any;


  ngOnInit() {
    this.fetchLight();
    this.intervalId = setInterval(() => this.fetchLight(), 1000);
  }

  ngOnDestroy() {
    if (this.intervalId) {
      clearInterval(this.intervalId);
    }
  }

  private fetchLight() {
    fetch('/light')
      .then(res => res.text())
      .then(val => {
        this.lightLevel = Number(val);
      });
  }
}
